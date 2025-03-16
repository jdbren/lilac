// Copyright (C) 2025 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include "paging.h"
#include "pgframe.h"

#include <lilac/lilac.h>

#define ENTRIES_PER_TABLE 512

#define is_canonical(addr) \
    (((addr) < 0x0000ffffffffffffUL) || \
     ((addr) > 0xffff000000000000UL))

#define MAX_PHYS_ADDR 0x0000ffffffffffffUL

typedef u64 pml4e_t;
typedef u64 pdpte_t;
typedef u64 pde_t;
typedef u64 pte_t;

#define ENTRY_ADDR(x) (phys_mem_mapping + (((x) & ~0xffful) & MAX_PHYS_ADDR))
#define ENTRY_PRESENT(x) ((x) & 1)

#define __native_flush_tlb_single(addr) \
   asm volatile("invlpg (%0)" : : "r"((uintptr_t)addr) : "memory");

// Maps all of physical memory
u8 *const phys_mem_mapping = (u8*)0xffff800000000000UL;

uintptr_t arch_get_pgd(void)
{
    uintptr_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}


int map_pages(void *phys, void *virt, u16 flags, int num_pages)
{
    flags |= 1;
    for (int i = 0; i < num_pages; i++, phys = (u8*)phys + PAGE_BYTES,
    virt = (u8*)virt + PAGE_BYTES) {
        pml4e_t *pml4 = (pml4e_t*)ENTRY_ADDR(arch_get_pgd());

        u32 pml4_ndx = (uintptr_t)virt >> 39 & 0x1FF;
        u32 pdpt_ndx = (uintptr_t)virt >> 30 & 0x1FF;
        u32 pd_ndx = (uintptr_t)virt >> 21 & 0x1FF;
        u32 pt_ndx = (uintptr_t)virt >> 12 & 0x1FF;

        pdpte_t *pdpt;
        if (!ENTRY_PRESENT(pml4[pml4_ndx])) {
            klog(LOG_DEBUG, "allocating pdpt for %x\n", virt);
            pml4[pml4_ndx] = (uintptr_t)alloc_frame() | flags;
            pml4[pml4_ndx] |= flags;
        }
        pdpt = (pdpte_t*)ENTRY_ADDR(pml4[pml4_ndx]);

        pde_t *pd;
        if (!ENTRY_PRESENT(pdpt[pdpt_ndx])) {
            klog(LOG_DEBUG, "allocating pd for %x\n", virt);
            pdpt[pdpt_ndx] = (uintptr_t)alloc_frame() | flags;
            pdpt[pdpt_ndx] |= flags;
        }
        pd = (pde_t*)ENTRY_ADDR(pdpt[pdpt_ndx]);

        pte_t *pt;
        if (!ENTRY_PRESENT(pd[pd_ndx])) {
            klog(LOG_DEBUG, "allocating pt for %x\n", virt);
            pd[pd_ndx] = (uintptr_t)alloc_frame() | flags;
            pd[pd_ndx] |= flags;
        }
        pt = (pte_t*)ENTRY_ADDR(pd[pd_ndx]);

        if (ENTRY_PRESENT(pt[pt_ndx])) {
            klog(LOG_WARN, "page already mapped\n");
            return 1;
        }
        pt[pt_ndx] = (uintptr_t)phys | flags;
        pt[pt_ndx] |= flags;
        __native_flush_tlb_single(virt);
    }
    return 0;
}

int unmap_pages(void *virt, int num_pages)
{
    return 0;
}


static pdpte_t phys_map_pdpt[ENTRIES_PER_TABLE] __align(PAGE_BYTES);

/*
    Set up the physical memory mapping using huge pages
*/
void init_phys_mem_mapping(size_t memory_sz_kb)
{
    extern size_t boot_pml4;
    // We still have identity mapped low memory
    pml4e_t *pml4 = (pml4e_t*)&boot_pml4;

    u32 pml4_ndx = (uintptr_t)phys_mem_mapping >> 39 & 0x1FF; // index 256
    pml4[pml4_ndx] = (pml4e_t)(pa(((uintptr_t)phys_map_pdpt) & ~0xfff) |
        PG_WRITE | PG_STRONG_UC | 1);

    printf("pml4[%d] = %x\n", pml4_ndx, pml4[pml4_ndx]);

    // Get mem size in GB
    u32 mem_size_gb = memory_sz_kb / (1024 * 1024);

    for (int i = 0; i <= mem_size_gb; i++) {
        u32 pdpt_ndx = (uintptr_t)(i*0x40000000UL) >> 30 & 0x1FF;
        phys_map_pdpt[pdpt_ndx] = (pdpte_t)((i*0x40000000UL) | PG_WRITE |
            PG_STRONG_UC | PG_HUGE_PAGE | 1);
    }
}

int kernel_pt_init(void) {return 0;}

void *get_physaddr(void *virt)
{
    return NULL;
}

static pde_t __pd_frame_bm[ENTRIES_PER_TABLE] __align(PAGE_BYTES);

int __map_frame_bm(void *phys, void *virt)
{
    extern size_t boot_pml4;
    // We still have identity mapped low memory
    pml4e_t *pml4 = (pml4e_t*)&boot_pml4;
    u32 pml4_ndx = (uintptr_t)virt >> 39 & 0x1FF; // should be mapped already in boot
    u32 pdpt_ndx = (uintptr_t)virt >> 30 & 0x1FF; // should be mapped already
    u32 pd_ndx = (uintptr_t)virt >> 21 & 0x1FF;
    if (!ENTRY_PRESENT(pml4[pml4_ndx])) {
        kerror("pdpt not present");
    }
    pdpte_t *pdpt = (pdpte_t*)ENTRY_ADDR(pml4[pml4_ndx]);
    if (!ENTRY_PRESENT(pdpt[pdpt_ndx])) {
        kerror("pd not present");
    }
    // Add a 2MB page
    pde_t *pd = (pde_t*)ENTRY_ADDR(pdpt[pdpt_ndx]);
    pd[pd_ndx] = (pde_t)((uintptr_t)phys | PG_WRITE | PG_STRONG_UC |
        PG_HUGE_PAGE | 1);
    __native_flush_tlb_single(virt);
    return 0;
}
