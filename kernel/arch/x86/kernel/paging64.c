// Copyright (C) 2025 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/lilac.h>
#include <lilac/boot.h>
#include <mm/kmm.h>
#include <mm/page.h>

#include "paging.h"

#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wanalyzer-out-of-bounds"

#define ENTRIES_PER_TABLE 512

#define MAX_PHYS_ADDR 0x0000ffffffffffffUL

#define ENTRY_ADDR(x) (phys_mem_mapping + (((x) & ~0xffful) & MAX_PHYS_ADDR))
#define ENTRY_PRESENT(x) ((x) & 1)

#define phys_to_mapping(x) (phys_mem_mapping + (x))

#define __native_flush_tlb_single(addr) \
   asm volatile("invlpg (%0)" : : "r"((uintptr_t)addr) : "memory");

// Maps all of physical memory
u8 *const phys_mem_mapping = (void*)__PHYS_MAP_ADDR;

uintptr_t arch_get_pgd(void)
{
    uintptr_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

pdpte_t * get_or_alloc_pdpt(pml4e_t *pml4, void *virt, u16 flags)
{
    u32 pml4_ndx = get_pml4_index(virt);
    if (!ENTRY_PRESENT(pml4[pml4_ndx])) {
        pml4[pml4_ndx] = (uintptr_t)alloc_frame() | flags | PG_WRITE;
        memset((void*)ENTRY_ADDR(pml4[pml4_ndx]), 0, PAGE_SIZE);
#ifdef DEBUG_PAGING
        klog(LOG_DEBUG, "Allocated PDPT at %p for %p\n",
            (void*)ENTRY_ADDR(pml4[pml4_ndx]), virt);
#endif
    }
    return (pdpte_t*)ENTRY_ADDR(pml4[pml4_ndx]);
}

pde_t * get_or_alloc_pd(pdpte_t *pdpt, void *virt, u16 flags)
{
    u32 pdpt_ndx = get_pdpt_index(virt);
    if (!ENTRY_PRESENT(pdpt[pdpt_ndx])) {
        pdpt[pdpt_ndx] = (uintptr_t)alloc_frame() | flags | PG_WRITE;
        memset((void*)ENTRY_ADDR(pdpt[pdpt_ndx]), 0, PAGE_SIZE);
#ifdef DEBUG_PAGING
        klog(LOG_DEBUG, "Allocated PD at %p for %p\n",
            (void*)ENTRY_ADDR(pdpt[pdpt_ndx]), virt);
#endif
    }
    return (pde_t*)ENTRY_ADDR(pdpt[pdpt_ndx]);
}

pte_t * get_or_alloc_pt(pde_t *pd, void *virt, u16 flags)
{
    u32 pd_ndx = get_pd_index(virt);
    if (!ENTRY_PRESENT(pd[pd_ndx])) {
        pd[pd_ndx] = (uintptr_t)alloc_frame() | flags;
        memset((void*)ENTRY_ADDR(pd[pd_ndx]), 0, PAGE_SIZE);
#ifdef DEBUG_PAGING
        klog(LOG_DEBUG, "Allocated PT at %p for %p\n",
            (void*)ENTRY_ADDR(pd[pd_ndx]), virt);
#endif
    }
    return (pte_t*)ENTRY_ADDR(pd[pd_ndx]);
}

int map_pages(void *phys, void *virt, int flags, int num_pages)
{
    flags |= x86_to_page_flags(flags) | PG_PRESENT;
    for (int i = 0; i < num_pages; i++, phys = (u8*)phys + PAGE_SIZE,
    virt = (u8*)virt + PAGE_SIZE) {
#ifdef DEBUG_PAGING
        printf("Mapping %p to %p with flags %x\n", virt, phys, flags);
#endif
        pml4e_t *pml4 = (pml4e_t*)ENTRY_ADDR(arch_get_pgd());
        pdpte_t *pdpt = get_or_alloc_pdpt(pml4, virt, flags);
        pde_t *pd = get_or_alloc_pd(pdpt, virt, flags);
        pte_t *pt = get_or_alloc_pt(pd, virt, flags);

        u32 pt_ndx = get_pt_index(virt);
        if (ENTRY_PRESENT(pt[pt_ndx])) {
            klog(LOG_ERROR, "page %p already mapped to %p\n", virt,
                (void*)((uintptr_t)pt[pt_ndx]));
            kerror("");
        }
        pt[pt_ndx] = (uintptr_t)phys | flags;
        __native_flush_tlb_single(virt);
    }
    return 0;
}

int unmap_pages(void *virt, int num_pages)
{
    for (int i = 0; i < num_pages; i++, virt = (u8*)virt + PAGE_SIZE) {
        pml4e_t *pml4 = (pml4e_t*)ENTRY_ADDR(arch_get_pgd());

        u32 pml4_ndx = get_pml4_index(virt);
        u32 pdpt_ndx = get_pdpt_index(virt);
        u32 pd_ndx = get_pd_index(virt);
        u32 pt_ndx = get_pt_index(virt);

        if (!ENTRY_PRESENT(pml4[pml4_ndx]))
            return -1;
        pdpte_t *pdpt = (pdpte_t*)ENTRY_ADDR(pml4[pml4_ndx]);

        if (!ENTRY_PRESENT(pdpt[pdpt_ndx]))
            return -1;
        pde_t *pd = (pde_t*)ENTRY_ADDR(pdpt[pdpt_ndx]);

        if (!ENTRY_PRESENT(pd[pd_ndx]))
            return -1;
        pte_t *pt = (pte_t*)ENTRY_ADDR(pd[pd_ndx]);

        if (!ENTRY_PRESENT(pt[pt_ndx]))
            return -1;
#ifdef DEBUG_PAGING
        printf("Unmapping %p from %p\n", virt, (void*)((uintptr_t)pt[pt_ndx] & ~0xFFF));
#endif
        pt[pt_ndx] = 0;
    }
    __native_flush_tlb_single(virt);
    return 0;
}


static pdpte_t phys_map_pdpt[ENTRIES_PER_TABLE] __align(PAGE_SIZE);

/*
    Set up the physical memory mapping using huge pages
*/
void init_phys_mem_mapping(size_t memory_sz_kb)
{
    extern size_t boot_pml4;
    // We still have identity mapped low memory
    pml4e_t *pml4 = (pml4e_t*)&boot_pml4;

    u32 pml4_ndx = get_pml4_index(phys_mem_mapping); // index 256
    pml4[pml4_ndx] = (pml4e_t)(__pa(((uintptr_t)phys_map_pdpt) & ~0xfff) |
        PG_WRITE | PG_STRONG_UC | 1);

    // Get mem size in GB
    u32 mem_size_gb = memory_sz_kb / (1024 * 1024);

    for (u32 i = 0; i <= mem_size_gb; i++) {
        u32 pdpt_ndx = get_pdpt_index(i*0x40000000UL);
        phys_map_pdpt[pdpt_ndx] = (pdpte_t)((i*0x40000000UL) | PG_WRITE |
            PG_STRONG_UC | PG_HUGE_PAGE | 1);
    }
}

// Preallocate pdpt mappings for the kheap area
int kernel_pt_init(uintptr_t start, uintptr_t end)
{
    pml4e_t *pml4 = (pml4e_t*)ENTRY_ADDR(arch_get_pgd());

    u32 pml4_ndx = get_pml4_index(start);

    assert(start < end);
    assert(start == (start & ~0x3fffffffUL));
    assert(end == (end & ~0x3fffffffUL));

    if (!ENTRY_PRESENT(pml4[pml4_ndx])) {
        pml4[pml4_ndx] = (pml4e_t)((uintptr_t)alloc_frame() | PG_WRITE |
            PG_WRITE_THROUGH | 1);
    }
    while (start != end) {
        u32 pdpt_ndx = get_pdpt_index(start);
        pdpte_t *pdpt = (pdpte_t*)ENTRY_ADDR(pml4[pml4_ndx]);
        if (ENTRY_PRESENT(pdpt[pdpt_ndx])) {
            kerror("PDPT was already present in kheap area");
        } else {
            pdpt[pdpt_ndx] = (pdpte_t)((uintptr_t)alloc_frame() | PG_WRITE |
                PG_STRONG_UC | 1);
            memset((void*)ENTRY_ADDR(pdpt[pdpt_ndx]), 0, PAGE_SIZE);
        }
        start += 0x40000000UL;
    }

    // Unmap the identity mapping
    memset(pml4, 0, 2048);
    __native_flush_tlb_single(0);
    return 0;
}

void *get_physaddr(void *virt)
{
    pml4e_t *pml4 = (pml4e_t*)ENTRY_ADDR(arch_get_pgd());
    u32 pml4_ndx = get_pml4_index(virt);
    u32 pdpt_ndx = get_pdpt_index(virt);

    if (!ENTRY_PRESENT(pml4[pml4_ndx]))
        return NULL;
    pdpte_t *pdpt = (pdpte_t*)ENTRY_ADDR(pml4[pml4_ndx]);
    if (!ENTRY_PRESENT(pdpt[pdpt_ndx]))
        return NULL;
    if (pdpt[pdpt_ndx] & PG_HUGE_PAGE)
        return (void*)((pdpt[pdpt_ndx] & ~0xFFF) + ((uintptr_t)virt & 0xFFF));
    pde_t *pd = (pde_t*)ENTRY_ADDR(pdpt[pdpt_ndx]);
    u32 pd_ndx = get_pd_index(virt);
    if (!ENTRY_PRESENT(pd[pd_ndx]))
        return NULL;
    if (pd[pd_ndx] & PG_HUGE_PAGE)
        return (void*)((pd[pd_ndx] & ~0xFFF) + ((uintptr_t)virt & 0xFFF));
    pte_t *pt = (pte_t*)ENTRY_ADDR(pd[pd_ndx]);
    u32 pt_ndx = get_pt_index(virt);
    if (!ENTRY_PRESENT(pt[pt_ndx]))
        return NULL;
    return (void*)((pt[pt_ndx] & ~0xFFF) + ((uintptr_t)virt & 0xFFF));
}

void * arch_map_frame_bitmap(size_t size)
{
    extern size_t boot_pml4;
    void *virt = (void*)(((uintptr_t)(&_kernel_end) & ~0x1fffffUL) + 0x200000);
    void *phys = (void*)__pa(virt);
    // We still have identity mapped low memory
    pml4e_t *pml4 = (pml4e_t*)&boot_pml4;
    u32 pml4_ndx = get_pml4_index(virt); // should be mapped already in boot
    u32 pdpt_ndx = get_pdpt_index(virt); // should be mapped already
    u32 pd_ndx = get_pd_index(virt);
    if (!ENTRY_PRESENT(pml4[pml4_ndx])) {
        kerror("pdpt not present");
    }
    pdpte_t *pdpt = (pdpte_t*)ENTRY_ADDR(pml4[pml4_ndx]);
    if (!ENTRY_PRESENT(pdpt[pdpt_ndx])) {
        kerror("pd not present");
    }
    // Add a 2MB page
    if (size > 0x200000)
        kerror("Cannot map more than 2MB for frame bitmap yet");
    pde_t *pd = (pde_t*)ENTRY_ADDR(pdpt[pdpt_ndx]);
    pd[pd_ndx] = (pde_t)((uintptr_t)phys | PG_WRITE | PG_STRONG_UC |
        PG_HUGE_PAGE | 1);
    __native_flush_tlb_single(virt);
    return virt;
}

void copy_kernel_mappings(uintptr_t phys_cr3)
{
    pml4e_t *new_cr3 = (pml4e_t*)phys_to_mapping(phys_cr3);
    pml4e_t *pml4 = (pml4e_t*)ENTRY_ADDR(arch_get_pgd());

    memset((void*)new_cr3, 0, 2048);

    // Skip to kernel space
    new_cr3 += 256;
    pml4 += 256;

    memcpy(new_cr3, pml4, 256 * sizeof(pml4e_t));
}
