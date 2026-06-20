// Copyright (C) 2025 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/lilac.h>
#include <lilac/boot.h>
#include <mm/kmm.h>
#include <mm/page.h>
#include <mm/mm.h>

#include "paging.h"

#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wanalyzer-out-of-bounds"

#define ENTRIES_PER_TABLE 512

#define MAX_PHYS_ADDR 0x0000ffffffffffffUL

#define PT_ADDR_MASK 0x000ffffffffff000UL
#define PT_FLAGS_MASK 0xfffUL
#define PT_ADDR(x) ((x) & PT_ADDR_MASK)

#define ENTRY_ADDR(x) (phys_mem_mapping + PT_ADDR(x))
#define ENTRY_PRESENT(x) ((x) & 1)

#define phys_to_mapping(x) (phys_mem_mapping + (x))

#define PDE_SIZE    0x200000UL       /* range of one PD entry (2MB) */
#define PDPTE_SIZE  0x40000000UL     /* range of one PDPT entry (1GB) */
#define PML4E_SIZE  0x8000000000UL   /* range of one PML4 entry (512GB) */

#define PDE_MASK    (~(PDE_SIZE   - 1))
#define PDPTE_MASK  (~(PDPTE_SIZE - 1))
#define PML4E_MASK  (~(PML4E_SIZE - 1))

/* Overflow-safe "next boundary" */
#define pde_addr_end(addr, end) \
    ({ uintptr_t __b = ((addr) + PDE_SIZE) & PDE_MASK;   \
       (__b - 1 < (end) - 1) ? __b : (end); })
#define pdpte_addr_end(addr, end) \
    ({ uintptr_t __b = ((addr) + PDPTE_SIZE) & PDPTE_MASK; \
       (__b - 1 < (end) - 1) ? __b : (end); })
#define pml4e_addr_end(addr, end) \
    ({ uintptr_t __b = ((addr) + PML4E_SIZE) & PML4E_MASK; \
       (__b - 1 < (end) - 1) ? __b : (end); })

// Maps all of physical memory
u8 *const phys_mem_mapping = (void*)__PHYS_MAP_ADDR;

pdpte_t * get_or_alloc_pdpt(pml4e_t *pml4, void *virt, u16 flags)
{
    u32 pml4_ndx = get_pml4_index(virt);
    if (!ENTRY_PRESENT(pml4[pml4_ndx])) {
#ifdef DEBUG_MM
    mm_dbg_page_table_pages_alloc++;
#endif
        pml4[pml4_ndx] = virt_to_phys(get_zeroed_page()) | flags | PG_WRITE |
            PG_PRESENT;
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
#ifdef DEBUG_MM
    mm_dbg_page_table_pages_alloc++;
#endif
        pdpt[pdpt_ndx] = virt_to_phys(get_zeroed_page()) | flags | PG_WRITE |
            PG_PRESENT;
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
#ifdef DEBUG_MM
    mm_dbg_page_table_pages_alloc++;
#endif
        pd[pd_ndx] = virt_to_phys(get_zeroed_page()) | flags | PG_WRITE |
            PG_PRESENT;
#ifdef DEBUG_PAGING
        klog(LOG_DEBUG, "Allocated PT at %p for %p\n",
            (void*)ENTRY_ADDR(pd[pd_ndx]), virt);
#endif
    }
    return (pte_t*)ENTRY_ADDR(pd[pd_ndx]);
}

int map_pages(void *phys, void *virt, int flags, int num_pages)
{
    pml4e_t *pml4 = (pml4e_t*)ENTRY_ADDR(arch_get_pgd());
    unsigned long pflags = x86_to_page_flags(flags);
    for (int i = 0; i < num_pages; i++, phys = (u8*)phys + PAGE_SIZE,
            virt = (u8*)virt + PAGE_SIZE) {
        // Intentionally shorten the flags to 16 bits to ignore XD bit for tables
        pdpte_t *pdpt = get_or_alloc_pdpt(pml4, virt, pflags & 0xFFFF);
        pde_t *pd = get_or_alloc_pd(pdpt, virt, pflags & 0xFFFF);
        pte_t *pt = get_or_alloc_pt(pd, virt, pflags & 0xFFFF);

        u32 pt_ndx = get_pt_index(virt);
        if (ENTRY_PRESENT(pt[pt_ndx])) {
            klog(LOG_ERROR, "page %p already mapped to %p\n", virt,
                (void*)((uintptr_t)pt[pt_ndx]));
            kerror("");
        }
        pt[pt_ndx] = (uintptr_t)phys | pflags;
        __native_flush_tlb_single(virt);
    }
    return 0;
}

static bool table_is_empty(u64 *table)
{
    for (int i = 0; i < ENTRIES_PER_TABLE; i++)
        if (ENTRY_PRESENT(table[i]))
            return false;
    return true;
}

static void unmap_pt_range(pde_t *pde, uintptr_t start, uintptr_t end)
{
    if (!ENTRY_PRESENT(*pde))
        return;

    pte_t *pt = (pte_t*)ENTRY_ADDR(*pde);
    pte_t *pte = pt + get_pt_index(start);

    for (; start < end; start += PAGE_SIZE, pte++) {
        *pte = 0;
        __native_flush_tlb_single((void*)start);
    }

    if (table_is_empty((u64*)pt)) {
        *pde = 0;
        free_page(pt);
    }
}

static void unmap_pd_range(pdpte_t *pdpte, uintptr_t start, uintptr_t end)
{
    if (!ENTRY_PRESENT(*pdpte))
        return;

    pde_t *pd = (pde_t*)ENTRY_ADDR(*pdpte);
    pde_t *pde = pd + get_pd_index(start);
    uintptr_t next;

    /* Not on a 2MB boundary? */
    if (start & (PDE_SIZE - 1)) {
        next = pde_addr_end(start, end);
        unmap_pt_range(pde, start, next);
        start = next;
        pde++;
    }

    /* Full 2MB chunks */
    for (; end - start >= PDE_SIZE; start += PDE_SIZE, pde++) {
        unmap_pt_range(pde, start, start + PDE_SIZE);
    }

    /* Remaining pages */
    if (start < end)
        unmap_pt_range(pde, start, end);

    if (table_is_empty((u64*)pd)) {
        *pdpte = 0;
        free_page(pd);
    }
}

static void unmap_pdpt_range(pml4e_t *pml4e, uintptr_t start, uintptr_t end, bool kernel_addr)
{
    if (!ENTRY_PRESENT(*pml4e))
        return;

    pdpte_t *pdpt = (pdpte_t*)ENTRY_ADDR(*pml4e);
    pdpte_t *pdpte = pdpt + get_pdpt_index(start);
    uintptr_t next;

    /* Not on a 1GB boundary? */
    if (start & (PDPTE_SIZE - 1)) {
        next = pdpte_addr_end(start, end);
        unmap_pd_range(pdpte, start, next);
        start = next;
        pdpte++;
    }

    /* Full 1GB chunks */
    for (; end - start >= PDPTE_SIZE; start += PDPTE_SIZE, pdpte++) {
        unmap_pd_range(pdpte, start, start + PDPTE_SIZE);
    }

    /* Remaining */
    if (start < end)
        unmap_pd_range(pdpte, start, end);

    if (!kernel_addr && table_is_empty((u64*)pdpt)) {
        *pml4e = 0;
        free_page(pdpt);
    }
}

int unmap_pages(void *virt, int num_pages)
{
    uintptr_t start = (uintptr_t)virt;
    uintptr_t end = start + (uintptr_t)num_pages * PAGE_SIZE;
    pml4e_t *pml4 = (pml4e_t*)ENTRY_ADDR(arch_get_pgd());
    pml4e_t *pml4e = pml4 + get_pml4_index(start);
    bool kernel_addr = start > __USER_MAX_ADDR;

#ifdef DEBUG_PAGING
    if (kernel_addr)
        klog(LOG_DEBUG, "Kernel addr unmap: %p - %p\n", (void*)start, (void*)end);
#endif

    /* Not on a 512GB boundary? */
    if (start & (PML4E_SIZE - 1)) {
        uintptr_t next = pml4e_addr_end(start, end);
        unmap_pdpt_range(pml4e, start, next, kernel_addr);
        start = next;
        pml4e++;
    }

    /* Full 512GB chunks */
    for (; end - start >= PML4E_SIZE; start += PML4E_SIZE, pml4e++) {
        unmap_pdpt_range(pml4e, start, start + PML4E_SIZE, kernel_addr);
    }

    /* Remaining */
    if (start < end)
        unmap_pdpt_range(pml4e, start, end, kernel_addr);

    return 0;
}

static void drop_user_pt_range(pde_t *pde, uintptr_t start, uintptr_t end)
{
    if (!ENTRY_PRESENT(*pde))
        return;

    pte_t *pt = (pte_t*)ENTRY_ADDR(*pde);
    pte_t *pte = pt + get_pt_index(start);

    for (; start < end; start += PAGE_SIZE, pte++) {
        pte_t pte_val = *pte;
        if (ENTRY_PRESENT(pte_val)) {
#ifdef DEBUG_MM
            mm_dbg_unmap_data_pages_freed++;
#endif
            struct page *pg = phys_to_page(PT_ADDR(pte_val));
            put_page(pg);
        }
        *pte = 0;
    }

    if (table_is_empty((u64*)pt)) {
        *pde = 0;
#ifdef DEBUG_MM
        mm_dbg_page_table_pages_freed++;
#endif
        free_page(pt);
    }
}

static void drop_user_pd_range(pdpte_t *pdpte, uintptr_t start, uintptr_t end)
{
    if (!ENTRY_PRESENT(*pdpte))
        return;

    pde_t *pd = (pde_t*)ENTRY_ADDR(*pdpte);
    pde_t *pde = pd + get_pd_index(start);
    uintptr_t next;

    /* Not on a 2MB boundary? */
    if (start & (PDE_SIZE - 1)) {
        next = pde_addr_end(start, end);
        drop_user_pt_range(pde, start, next);
        start = next;
        pde++;
    }

    /* Full 2MB chunks */
    for (; end - start >= PDE_SIZE; start += PDE_SIZE, pde++) {
        drop_user_pt_range(pde, start, start + PDE_SIZE);
    }

    /* Remaining pages */
    if (start < end)
        drop_user_pt_range(pde, start, end);

    if (table_is_empty((u64*)pd)) {
        *pdpte = 0;
#ifdef DEBUG_MM
        mm_dbg_page_table_pages_freed++;
#endif
        free_page(pd);
    }
}

static void drop_user_pdpt_range(pml4e_t *pml4e, uintptr_t start, uintptr_t end)
{
    if (!ENTRY_PRESENT(*pml4e))
        return;

    pdpte_t *pdpt = (pdpte_t*)ENTRY_ADDR(*pml4e);
    pdpte_t *pdpte = pdpt + get_pdpt_index(start);
    uintptr_t next;

    /* Not on a 1GB boundary? */
    if (start & (PDPTE_SIZE - 1)) {
        next = pdpte_addr_end(start, end);
        drop_user_pd_range(pdpte, start, next);
        start = next;
        pdpte++;
    }

    /* Full 1GB chunks */
    for (; end - start >= PDPTE_SIZE; start += PDPTE_SIZE, pdpte++) {
        drop_user_pd_range(pdpte, start, start + PDPTE_SIZE);
    }

    /* Remaining */
    if (start < end)
        drop_user_pd_range(pdpte, start, end);

    if (table_is_empty((u64*)pdpt)) {
        *pml4e = 0;
#ifdef DEBUG_MM
        mm_dbg_page_table_pages_freed++;
#endif
        free_page(pdpt);
    }
}

void drop_user_page_range(uintptr_t start, size_t size)
{
    uintptr_t end = PAGE_ROUND_UP(start + size);
    pml4e_t *pml4 = (pml4e_t*)ENTRY_ADDR(arch_get_pgd());
    pml4e_t *pml4e = pml4 + get_pml4_index(start);

#ifdef DEBUG_PAGING
    klog(LOG_DEBUG, "Dropping user page range %p - %p\n", (void*)start, (void*)end);
#endif

    if (end > __USER_MAX_ADDR + 1)
        panic("Tried to drop kernel addr range: %p - %p\n", (void*)start, (void*)end);

    /* Not on a 512GB boundary? */
    if (start & (PML4E_SIZE - 1)) {
        uintptr_t next = pml4e_addr_end(start, end);
        drop_user_pdpt_range(pml4e, start, next);
        start = next;
        pml4e++;
    }

    /* Full 512GB chunks */
    for (; end - start >= PML4E_SIZE; start += PML4E_SIZE, pml4e++) {
        drop_user_pdpt_range(pml4e, start, start + PML4E_SIZE);
    }

    /* Remaining */
    if (start < end)
        drop_user_pdpt_range(pml4e, start, end);
}

static void update_user_pt_range(pde_t *pde, uintptr_t start,
    uintptr_t end, unsigned long new_flags)
{
    if (!ENTRY_PRESENT(*pde))
        return;

    if (*pde & PG_HUGE_PAGE) {
        *pde = PT_ADDR(*pde) | new_flags | PG_HUGE_PAGE;
        __native_flush_tlb_single((void*)start);
        return;
    }

    pte_t *pt = (pte_t*)ENTRY_ADDR(*pde);
    pte_t *pte = pt + get_pt_index(start);

    for (; start < end; start += PAGE_SIZE, pte++) {
        if (ENTRY_PRESENT(*pte)) {
            *pte = PT_ADDR(*pte) | new_flags;
            __native_flush_tlb_single((void*)start);
        }
    }
}

static void update_user_pd_range(pdpte_t *pdpte, uintptr_t start,
    uintptr_t end, unsigned long new_flags)
{
    if (!ENTRY_PRESENT(*pdpte))
        return;

    if (*pdpte & PG_HUGE_PAGE) {
        *pdpte = PT_ADDR(*pdpte) | new_flags | PG_HUGE_PAGE;
        __native_flush_tlb_single((void*)start);
        return;
    }

    pde_t *pd = (pde_t*)ENTRY_ADDR(*pdpte);
    pde_t *pde = pd + get_pd_index(start);
    uintptr_t next;

    // Not on a 2MB boundary?
    if (start & (PDE_SIZE - 1)) {
        next = pde_addr_end(start, end);
        update_user_pt_range(pde, start, next, new_flags);
        start = next;
        pde++;
    }

    // Full 2MB chunks
    for (; end - start >= PDE_SIZE; start += PDE_SIZE, pde++) {
        update_user_pt_range(pde, start, start + PDE_SIZE, new_flags);
    }

    // Remaining pages
    if (start < end)
        update_user_pt_range(pde, start, end, new_flags);
}

static void update_user_pdpt_range(pml4e_t *pml4e, uintptr_t start,
    uintptr_t end, unsigned long new_flags)
{
    if (!ENTRY_PRESENT(*pml4e))
        return;

    pdpte_t *pdpt = (pdpte_t*)ENTRY_ADDR(*pml4e);
    pdpte_t *pdpte = pdpt + get_pdpt_index(start);
    uintptr_t next;

    // Not on a 1GB boundary?
    if (start & (PDPTE_SIZE - 1)) {
        next = pdpte_addr_end(start, end);
        update_user_pd_range(pdpte, start, next, new_flags);
        start = next;
        pdpte++;
    }

    // Full 1GB chunks
    for (; end - start >= PDPTE_SIZE; start += PDPTE_SIZE, pdpte++) {
        update_user_pd_range(pdpte, start, start + PDPTE_SIZE, new_flags);
    }

    // Remaining
    if (start < end)
        update_user_pd_range(pdpte, start, end, new_flags);
}

void update_user_page_range(uintptr_t start, size_t size, int flags)
{
    uintptr_t end = PAGE_ROUND_UP(start + size);
    pml4e_t *pml4 = (pml4e_t*)ENTRY_ADDR(arch_get_pgd());
    pml4e_t *pml4e = pml4 + get_pml4_index(start);
    unsigned long new_flags = x86_to_page_flags(flags);

    if (end > __USER_MAX_ADDR + 1)
        panic("Tried to update kernel addr range: %p - %p\n", (void*)start, (void*)end);

    // Not on a 512GB boundary?
    if (start & (PML4E_SIZE - 1)) {
        uintptr_t next = pml4e_addr_end(start, end);
        update_user_pdpt_range(pml4e, start, next, new_flags);
        start = next;
        pml4e++;
    }

    // Full 512GB chunks
    for (; end - start >= PML4E_SIZE; start += PML4E_SIZE, pml4e++) {
        update_user_pdpt_range(pml4e, start, start + PML4E_SIZE, new_flags);
    }

    // Remaining
    if (start < end)
        update_user_pdpt_range(pml4e, start, end, new_flags);
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
        PG_WRITE | PG_PRESENT);

    // Get mem size in GB
    u32 mem_size_gb = memory_sz_kb / (1024 * 1024);

    for (u32 i = 0; i <= mem_size_gb; i++) {
        u32 pdpt_ndx = get_pdpt_index(i*0x40000000UL);
        phys_map_pdpt[pdpt_ndx] = (pdpte_t)((i*0x40000000UL) | PG_WRITE |
            PG_HUGE_PAGE | PG_GLOBAL | PG_PRESENT);
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
        pml4[pml4_ndx] = (pml4e_t)(virt_to_phys(get_zeroed_page()) | PG_WRITE |
            PG_PRESENT);
    }
    while (start != end) {
        u32 pdpt_ndx = get_pdpt_index(start);
        pdpte_t *pdpt = (pdpte_t*)ENTRY_ADDR(pml4[pml4_ndx]);
        if (ENTRY_PRESENT(pdpt[pdpt_ndx])) {
            kerror("PDPT was already present in kheap area");
        } else {
            pdpt[pdpt_ndx] = (pdpte_t)(virt_to_phys(get_zeroed_page())
                | PG_WRITE | PG_GLOBAL | PG_PRESENT);
        }
        start += 0x40000000UL;
    }

    // Unmap the identity mapping
    memset(pml4, 0, 2048);
    __native_flush_tlb();
    return 0;
}

void *__get_physaddr(void *virt)
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
        return (void*)(PT_ADDR(pdpt[pdpt_ndx]) + ((uintptr_t)virt & 0xFFF));
    pde_t *pd = (pde_t*)ENTRY_ADDR(pdpt[pdpt_ndx]);
    u32 pd_ndx = get_pd_index(virt);
    if (!ENTRY_PRESENT(pd[pd_ndx]))
        return NULL;
    if (pd[pd_ndx] & PG_HUGE_PAGE)
        return (void*)(PT_ADDR(pd[pd_ndx]) + ((uintptr_t)virt & 0xFFF));
    pte_t *pt = (pte_t*)ENTRY_ADDR(pd[pd_ndx]);
    u32 pt_ndx = get_pt_index(virt);
    if (!ENTRY_PRESENT(pt[pt_ndx]) && !(pt[pt_ndx] & PG_PROT_NONE)) {
        klog(LOG_WARN, "pte marked not present for %p, pte = %lx\n", virt, pt[pt_ndx]);
        return NULL;
    }
    return (void*)(PT_ADDR(pt[pt_ndx]) + ((uintptr_t)virt & 0xFFF));
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
    pd[pd_ndx] = (pde_t)((uintptr_t)phys | PG_WRITE | PG_HUGE_PAGE | PG_PRESENT);
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
