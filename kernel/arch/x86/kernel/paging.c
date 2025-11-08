// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/pmem.h>
#include <lilac/boot.h>
#include <lilac/panic.h>
#include <lilac/libc.h>
#include "paging.h"

#define PAGE_DIR_SIZE 1024
#define PAGE_TABLE_SIZE 1024

typedef u32 pde_t;
static u32* const pd = (u32*)0xFFFFF000;

static int pde(int index, u16 flags);
static inline void __native_flush_tlb_single(uintptr_t addr)
{
   asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

uintptr_t arch_get_pgd(void)
{
    uintptr_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
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
    flags |= 1;
    for (int i = 0; i < num_pages; i++, physaddr = (u8*)physaddr + PAGE_BYTES,
    virtualaddr = (u8*)virtualaddr + PAGE_BYTES) {
        //printf("mapping %x to %x\n", physaddr, virtualaddr);
        assert(is_aligned(physaddr, PAGE_BYTES));
        assert(is_aligned(virtualaddr, PAGE_BYTES));

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

        __native_flush_tlb_single((u32)virtualaddr);
    }

    return 0;
}

int unmap_pages(void *virtualaddr, int num_pages)
{
    for (int i = 0; i < num_pages; i++, virtualaddr = (u8*)virtualaddr + PAGE_BYTES) {
        //printf("unmapping %x\n", virtualaddr);
        assert(is_aligned(virtualaddr, PAGE_BYTES));

        u32 pdindex = (u32)virtualaddr >> 22;
        u32 ptindex = (u32)virtualaddr >> 12 & 0x03FF;

        if (!pd[pdindex] & 0x1)
            return 1;

        u32 *pt = ((u32*)0xFFC00000) + (0x400 * pdindex);
        if (!pt[ptindex] & 0x1)
            return 1;

        pt[ptindex] = 0; // set not present

        __native_flush_tlb_single((u32)virtualaddr);
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
    assert(is_aligned(addr, PAGE_BYTES));

    pde_t entry = (u32)addr | flags;
    pd[index] = entry;

    volatile u32 *pt = ((u32*)0xFFC00000) + (0x400 * index);
    memset((void*)pt, 0, PAGE_BYTES);

    // printf("pde %x: %x\n", index, entry);

    return 0;
}

void * arch_map_frame_bitmap(size_t size)
{
    void *virt = (void*)(((uintptr_t)&_kernel_end + 0xfff) & ~0xfff);
    void *phys = (void*)get_phys_addr(virt);
    map_pages(phys, virt, PG_WRITE, size / PAGE_SIZE + 1);
    return virt;
}

void init_phys_mem_mapping(size_t) {}
