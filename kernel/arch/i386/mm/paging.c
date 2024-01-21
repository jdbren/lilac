#include <stdint.h>
#include <string.h>

#include <kernel/panic.h>
#include <mm/paging.h>
#include <mm/pgframe.h>

#define PAGE_DIR_SIZE 1024
#define PAGE_TABLE_SIZE 1024

#define is_aligned(x, align) (((uintptr_t)x) % (align) == 0)

typedef uint32_t pde_t;
static uint32_t* const pd = (uint32_t*)0xFFFFF000;

static int pde(int index, uint8_t priv, uint8_t rw);

static inline void __native_flush_tlb_single(uint32_t addr) {
   asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

void *get_physaddr(void *virtualaddr) {
    uint32_t pdindex = (uint32_t)virtualaddr >> 22;
    uint32_t ptindex = (uint32_t)virtualaddr >> 12 & 0x03FF;
 
    if(!pd[pdindex] & 0x1) 
        return 0;
 
    uint32_t *pt = ((uint32_t *)0xFFC00000) + (0x400 * pdindex);
    if(pt[ptindex] & 0x1) 
        return 0;

    return (void *)((pt[ptindex] & ~0xFFF) + ((uint32_t)virtualaddr & 0xFFF));
}

int map_pages(void *physaddr, void *virtualaddr, uint16_t flags, int num_pages) {
    for(int i = 0; i < num_pages; i++, 
        physaddr += PAGE_BYTES, virtualaddr += PAGE_BYTES) 
    {
        assert(is_aligned(physaddr, PAGE_BYTES));
        assert(is_aligned(virtualaddr, PAGE_BYTES));
    
        uint32_t pdindex = (uint32_t)virtualaddr >> 22;
        uint32_t ptindex = (uint32_t)virtualaddr >> 12 & 0x03FF;

        if(!(pd[pdindex] & 0x1))
            pde(pdindex, 0, 1);
    
        uint32_t *pt = ((uint32_t*)0xFFC00000) + (0x400 * pdindex);
        if(pt[ptindex] & 0x1) {
            printf("mapping already present\n");
            return -1;
        }

        pt[ptindex] = ((uint32_t)physaddr) | (flags & 0xFFF) | 0x01; // Present
    
        __native_flush_tlb_single((uint32_t)virtualaddr);
        printf("Mapped page %x to %x\n", physaddr, virtualaddr);
    }

    return 0;
}

int unmap_pages(void *virtualaddr, int num_pages) {
    for(int i = 0; i < num_pages; i++, virtualaddr += PAGE_BYTES) {
        assert(is_aligned(virtualaddr, PAGE_BYTES));
    
        uint32_t pdindex = (uint32_t)virtualaddr >> 22;
        uint32_t ptindex = (uint32_t)virtualaddr >> 12 & 0x03FF;
    
        if(!pd[pdindex] & 0x1)
            return -1;
    
        uint32_t *pt = ((uint32_t*)0xFFC00000) + (0x400 * pdindex);
        if(!pt[ptindex] & 0x1)
            return -1;
    
        pt[ptindex] &= ~1; // set not present
    
        __native_flush_tlb_single((uint32_t)virtualaddr);

    }

    return 0;
}

static int pde(int index, uint8_t priv, uint8_t rw) {
    static const uint8_t default_flags = 0x1; // present 
    // 3...0: cache enabled, write-through disabled, u/s mode
    uint8_t flags = default_flags | (priv << 2) | (rw << 1);

    void *addr = alloc_frame(1);
    assert(is_aligned(addr, PAGE_BYTES));

    pde_t entry = (uint32_t)addr | flags;
    printf("Allocated page %x for page table %d\n", addr, index);
    pd[index] = entry;

    uint32_t *pt = ((uint32_t*)0xFFC00000) + (0x400 * index);
    memset(pt, 0, PAGE_BYTES);

    return 0;
}
