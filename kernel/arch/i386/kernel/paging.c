#include <kernel/types.h>
#include <string.h>

#include <kernel/panic.h>
#include <paging.h>
#include <pgframe.h>

#define PAGE_DIR_SIZE 1024
#define PAGE_TABLE_SIZE 1024

#define is_aligned(x, align) (((uintptr_t)x) % (align) == 0)

typedef u32 pde_t;
static u32* const pd = (u32*)0xFFFFF000;

static int pde(int index, u8 priv, u8 rw);
static inline void __native_flush_tlb_single(u32 addr) 
{
   asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

void *get_physaddr(void *virtualaddr) 
{
    u32 pdindex = (u32)virtualaddr >> 22;
    u32 ptindex = (u32)virtualaddr >> 12 & 0x03FF;
 
    u32 *pd = (u32 *)0xFFFFF000;
    if (!(pd[pdindex] & 0x1))
        return NULL;
 
    u32 *pt = ((u32 *)0xFFC00000) + (0x400 * pdindex);
    if (!(pt[ptindex] & 0x1))
        return NULL;
 
    return (void *)((pt[ptindex] & ~0xFFF) + ((u32)virtualaddr & 0xFFF));
}

int map_pages(void *physaddr, void *virtualaddr, u16 flags, int num_pages) 
{
    for (int i = 0; i < num_pages; i++, physaddr += PAGE_BYTES, 
    virtualaddr += PAGE_BYTES) {
        assert(is_aligned(physaddr, PAGE_BYTES));
        assert(is_aligned(virtualaddr, PAGE_BYTES));
    
        u32 pdindex = (u32)virtualaddr >> 22;
        u32 ptindex = (u32)virtualaddr >> 12 & 0x03FF;

        if (!(pd[pdindex] & 0x1))
            pde(pdindex, 0, 1);
    
        u32 *pt = ((u32*)0xFFC00000) + (0x400 * pdindex);
        if (pt[ptindex] & 0x1) {
            printf("mapping already present\n");
            return 1;
        }

        pt[ptindex] = ((u32)physaddr) | (flags & 0xFFF) | 0x01; // Present
    
        __native_flush_tlb_single((u32)virtualaddr);

        //printf("Mapped %x to %x\n", physaddr, virtualaddr);
    }

    return 0;
}

int unmap_pages(void *virtualaddr, int num_pages) 
{
    for (int i = 0; i < num_pages; i++, virtualaddr += PAGE_BYTES) {
        assert(is_aligned(virtualaddr, PAGE_BYTES));
    
        u32 pdindex = (u32)virtualaddr >> 22;
        u32 ptindex = (u32)virtualaddr >> 12 & 0x03FF;
    
        if (!pd[pdindex] & 0x1)
            return 1;
    
        u32 *pt = ((u32*)0xFFC00000) + (0x400 * pdindex);
        if (!pt[ptindex] & 0x1)
            return 1;
    
        pt[ptindex] &= ~1; // set not present
    
        __native_flush_tlb_single((u32)virtualaddr);
    }

    return 0;
}

static int pde(int index, u8 priv, u8 rw) 
{
    static const u8 default_flags = 0x1; // present 

    // 3...0: cache enabled, write-through disabled, u/s mode
    u8 flags = default_flags | (priv << 2) | (rw << 1);

    void *addr = alloc_frame();
    assert(is_aligned(addr, PAGE_BYTES));

    pde_t entry = (u32)addr | flags;
    pd[index] = entry;

    u32 *pt = ((u32*)0xFFC00000) + (0x400 * index);
    memset(pt, 0, PAGE_BYTES);

    //printf("Allocated page %x for page table %d\n", addr, index);

    return 0;
}
