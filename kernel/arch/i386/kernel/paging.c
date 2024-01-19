#include <stdint.h>
#include <string.h>
#include <kernel/paging.h>
#include <kernel/panic.h>
#include <kernel/pgframe.h>

#define assert(x) if (!(x)) kerror("Assertion failed: " #x)
#define is_aligned(x) (((uint32_t)(x) & 0xFFF) == 0)

#define PAGE_DIR_SIZE 1024
#define PAGE_TABLE_SIZE 1024

typedef uint32_t pde_t;

extern pde_t page_directory[PAGE_DIR_SIZE];

static int pde(int index, uint8_t priv, uint8_t rw);

static inline void __native_flush_tlb_single(uint32_t addr) {
   asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

void *get_physaddr(void *virtualaddr) {
    uint32_t pdindex = (uint32_t)virtualaddr >> 22;
    uint32_t ptindex = (uint32_t)virtualaddr >> 12 & 0x03FF;
 
    uint32_t *pd = (uint32_t *)0xFFFFF000;
    if(!pd[pdindex] & 0x1) 
        return 0;
 
    uint32_t *pt = ((uint32_t *)0xFFC00000) + (0x400 * pdindex);
    if(pt[ptindex] & 0x1) 
        return 0;

    return (void *)((pt[ptindex] & ~0xFFF) + ((uint32_t)virtualaddr & 0xFFF));
}

int map_page(void *physaddr, void *virtualaddr, uint16_t flags) {
    assert(is_aligned(physaddr));
    assert(is_aligned(virtualaddr));
 
    uint32_t pdindex = (uint32_t)virtualaddr >> 22;
    uint32_t ptindex = (uint32_t)virtualaddr >> 12 & 0x03FF;
 
    uint32_t *pd = (uint32_t *)0xFFFFF000;

    if(!(pd[pdindex] & 0x1)) {
        pde(pdindex, 0, 1);
        //memset((void*) pd[pdindex], 0, PAGE_BYTES);
    }
 
    uint32_t *pt = ((uint32_t *)0xFFC00000) + (0x400 * pdindex);
    // Here you need to check whether the PT entry is present.
    // When it is, then there is already a mapping present. What do you do now?
    if(pt[ptindex] & 0x1) {
        printf("mapping already present\n");
        //return -1;
    }
    pt[ptindex] = ((uint32_t)physaddr) | (flags & 0xFFF) | 0x01; // Present
 
    // Now you need to flush the entry in the TLB
    // or you might not notice the change.
    __native_flush_tlb_single((uint32_t)virtualaddr);

    return 0;
}

int unmap_page(void *virtualaddr) {
    assert(is_aligned(virtualaddr));
 
    uint32_t pdindex = (uint32_t)virtualaddr >> 22;
    uint32_t ptindex = (uint32_t)virtualaddr >> 12 & 0x03FF;
 
    uint32_t *pd = (uint32_t*)0xFFFFF000;
    if(!pd[pdindex] & 0x1)
        return -1;
 
    uint32_t *pt = ((uint32_t*)0xFFC00000) + (0x400 * pdindex);
    if(!pt[ptindex] & 0x1)
        return -1;
 
    pt[ptindex] &= ~1; // set not present
 
    // Now you need to flush the entry in the TLB
    // or you might not notice the change.
    __native_flush_tlb_single((uint32_t)virtualaddr);

    return 0;
}

static int pde(int index, uint8_t priv, uint8_t rw) {
    static const uint8_t default_flags = 0x1; // present 
    // 3...0: cache enabled, write-through disabled, u/s mode
    uint8_t flags = default_flags | (priv << 2) | (rw << 1);

    void *addr = alloc_frame(1);
    assert(is_aligned(addr));

    pde_t entry = (uint32_t)addr | flags;
    //printf("Allocated page: %x for page table\n", entry);
    page_directory[index] = entry;

    printf("Allocated page table for index %d\n", index);

    return 0;
}
