#include <stdint.h>
#include <kernel/panic.h>

#define assert(x) if (!(x)) kerror("Assertion failed: " #x)
#define is_aligned(x) (((uint32_t)(x) & 0xFFF) == 0)

#define PAGE_BYTES 4096
#define PAGE_DIR_SIZE 1024
#define PAGE_TABLE_SIZE 1024

typedef uint32_t PageDirEntry;

extern PageDirEntry page_directory[PAGE_DIR_SIZE];

static inline void __native_flush_tlb_single(unsigned long addr) {
   asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static void page_dir_entry(int index, uint32_t addr, uint8_t priv, uint8_t rw) {
    static const uint8_t default_flags = 0x1; // 0000 0001, present set
    // 3...0: cache enabled, write-through disabled, u/s mode, kernel will ignore rw
    uint8_t flags = default_flags | (priv << 2) | (rw << 1);

    if(addr > 0xFFFFFF) {
        kerror("Address too large for page directory entry");
    }
    PageDirEntry entry = (addr << 12) | flags;
    page_directory[index] = entry;
}

void *get_physaddr(void *virtualaddr) {
    uint32_t pdindex = (uint32_t)virtualaddr >> 22;
    uint32_t ptindex = (uint32_t)virtualaddr >> 12 & 0x03FF;
 
    uint32_t *pd = (uint32_t*)0xFFFFF000;
    // Here you need to check whether the PD entry is present.
    if(!(pd[pdindex] & 0x1)) {
        return 0;
    }
 
    uint32_t *pt = ((uint32_t*)0xFFC00000) + (0x400 * pdindex);
    // Here you need to check whether the PT entry is present.
    if(!(pt[ptindex] & 0x1)) {
        return 0;
    }
 
    return (void *)((pt[ptindex] & ~0xFFF) + ((uint32_t)virtualaddr & 0xFFF));
}

void map_page(void *physaddr, void *virtualaddr, unsigned int flags) {
    // Make sure that both addresses are page-aligned.
    assert(is_aligned(physaddr));
 
    uint32_t pdindex = (uint32_t)virtualaddr >> 22;
    uint32_t ptindex = (uint32_t)virtualaddr >> 12 & 0x03FF;
 
    uint32_t *pd = (uint32_t*)0xFFFFF000;
    // Here you need to check whether the PD entry is present.
    // When it is not present, you need to create a new empty PT and
    // adjust the PDE accordingly.
    if(!(pd[pdindex] & 0x1)) {
        page_dir_entry(pdindex, 0, 0);
    }

 
    uint32_t *pt = ((uint32_t*)0xFFC00000) + (0x400 * pdindex);
    // Here you need to check whether the PT entry is present.
    // When it is, then there is already a mapping present. What do you do now?
 
    pt[ptindex] = ((uint32_t)physaddr) | (flags & 0xFFF) | 0x01; // Present
 
    // Now you need to flush the entry in the TLB
    // or you might not notice the change.
    __native_flush_tlb_single((uint32_t)virtualaddr);
}

void unmap_page(void *virtualaddr) {
    // Make sure that both addresses are page-aligned.
 
    uint32_t pdindex = (uint32_t)virtualaddr >> 22;
    uint32_t ptindex = (uint32_t)virtualaddr >> 12 & 0x03FF;
 
    uint32_t *pd = (uint32_t*)0xFFFFF000;
    // Here you need to check whether the PD entry is present.
 
    uint32_t *pt = ((uint32_t*)0xFFC00000) + (0x400 * pdindex);
    // Here you need to check whether the PT entry is present.
 
    pt[ptindex] = 0x00000000; // Not present
 
    // Now you need to flush the entry in the TLB
    // or you might not notice the change.
    __native_flush_tlb_single((uint32_t)virtualaddr);
}
