// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <mm/page.h>
#include <lilac/boot.h>
#include <lilac/panic.h>
#include <lilac/libc.h>
#include "paging.h"

#define PAGE_DIR_SIZE 1024
#define PAGE_TABLE_SIZE 1024

typedef u32 pde_t;
static u32* const pd = (u32*)0xFFFFF000;

static int pde(int index, u16 flags);

void *__get_physaddr(void *virtualaddr)
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

int map_pages(void *physaddr, void *virtualaddr, int flags, int num_pages)
{
    flags |= x86_to_page_flags(flags) | 1;
    for (int i = 0; i < num_pages; i++, physaddr = (u8*)physaddr + PAGE_SIZE,
    virtualaddr = (u8*)virtualaddr + PAGE_SIZE) {
        //printf("mapping %x to %x\n", physaddr, virtualaddr);
        assert(is_aligned(physaddr, PAGE_SIZE));
        assert(is_aligned(virtualaddr, PAGE_SIZE));

        u32 pdindex = (u32)virtualaddr >> 22;
        u32 ptindex = (u32)virtualaddr >> 12 & 0x03FF;

        //printf("pdindex: %x, ptindex: %x\n", pdindex, ptindex);

        if (!(pd[pdindex] & 0x1))
            pde(pdindex, flags);

        u32 *pt = ((u32*)0xFFC00000) + (0x400 * pdindex);
        if (pt[ptindex] & 0x1) {
            printf("mapping already present\n");
            printf("physaddr: %x, virtualaddr: %x\n", pt[ptindex], virtualaddr);
            return 1;
        }

        pt[ptindex] = ((u32)physaddr) | (flags & 0xFFF);

        __native_flush_tlb_single(virtualaddr);
    }

    return 0;
}

int unmap_pages(void *virtualaddr, int num_pages)
{
    for (int i = 0; i < num_pages; i++, virtualaddr = (u8*)virtualaddr + PAGE_SIZE) {
        //printf("unmapping %x\n", virtualaddr);
        assert(is_aligned(virtualaddr, PAGE_SIZE));

        u32 pdindex = (u32)virtualaddr >> 22;
        u32 ptindex = (u32)virtualaddr >> 12 & 0x03FF;

        if (!pd[pdindex] & 0x1)
            return 1;

        u32 *pt = ((u32*)0xFFC00000) + (0x400 * pdindex);
        if (!pt[ptindex] & 0x1)
            return 1;

        pt[ptindex] = 0; // set not present

        __native_flush_tlb_single(virtualaddr);
    }

    return 0;
}

// Allocate mem for kernel page tables
int kernel_pt_init(uintptr_t u1, uintptr_t u2)
{
    for (int i = PG_DIR_INDEX(0xC0000000UL); i < PAGE_DIR_SIZE; i++) {
        if (!(pd[i] & 1))
            pde(i, PG_WRITE);
    }
    return 0;
}

static int pde(int index, u16 flags)
{
    static const u8 default_flags = PG_WRITE | 1;
    flags |= default_flags;

    void *addr = alloc_frame();
    assert(is_aligned(addr, PAGE_SIZE));

    pde_t entry = (u32)addr | flags;
    pd[index] = entry;

    volatile u32 *pt = ((u32*)0xFFC00000) + (0x400 * index);
    memset((void*)pt, 0, PAGE_SIZE);

    // printf("pde %x: %x\n", index, entry);

    return 0;
}

void * arch_map_frame_bitmap(size_t size)
{
    void *virt = (void*)(((uintptr_t)&_kernel_end + 0xfff) & ~0xfff);
    void *phys = (void*)__pa(virt);
    map_pages(phys, virt, PG_WRITE, size / PAGE_SIZE + 1);
    return virt;
}

u8 *const phys_mem_mapping = (void*)__PHYS_MAP_ADDR;

void init_phys_mem_mapping(size_t mem_sz_kb) {
    size_t mem_map_space = KHEAP_START_ADDR - __PHYS_MAP_ADDR;
    size_t required_space = mem_sz_kb * 1024;
    if (required_space > mem_map_space) {
        klog(LOG_WARN, "Only %lu KB of physical memory can be mapped\n",
             mem_map_space / 1024);
        required_space = mem_map_space;
    }

    uintptr_t virt = __PHYS_MAP_ADDR;
    uintptr_t phys = 0;
    uintptr_t end  = __PHYS_MAP_ADDR + required_space;

    while (virt + 0x400000 <= end) {
        u32 pdindex = PG_DIR_INDEX(virt);
        pd[pdindex] = phys | PG_WRITE | PG_HUGE_PAGE | 1;
        virt += 0x400000;
        phys += 0x400000;
    }

    __native_flush_tlb();
}
