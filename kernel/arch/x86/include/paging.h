// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef x86_PAGING_H
#define x86_PAGING_H

#include <lilac/types.h>

#define PAGE_BYTES 4096

#define is_aligned(POINTER, BYTE_COUNT) \
    (((uintptr_t)(POINTER)) % (BYTE_COUNT) == 0)

#ifdef __x86_64__
typedef u64 pml4e_t;
typedef u64 pdpte_t;
typedef u64 pde_t;
typedef u64 pte_t;
#else
typedef u32 pdpte_t;
typedef u32 pde_t;
typedef u32 pte_t;
#endif

#ifdef __x86_64__
#define get_pml4_index(x) ((uintptr_t)(x) >> 39 & 0x1FF)
#define get_pdpt_index(x) ((uintptr_t)(x) >> 30 & 0x1FF)
#define get_pd_index(x) ((uintptr_t)(x) >> 21 & 0x1FF)
#define get_pt_index(x) ((uintptr_t)(x) >> 12 & 0x1FF)
#define get_page_offset(x) ((uintptr_t)(x) & 0xfff)
#else
#define PG_DIR_INDEX(x) (((x) >> 22) & 0x3FF)
#define PG_TABLE_INDEX(x) (((x) >> 12) & 0x3FF)
#endif

#define PG_PRESENT         0x1
#define PG_READ            0x0
#define PG_WRITE           0x2
#define PG_SUPER           0x0
#define PG_USER            0x4
#define PG_WRITE_THROUGH   0x8
#define PG_CACHE_DISABLE   0x10
#define PG_ACCESSED        0x20
#define PG_DIRTY           0x40
#define PG_HUGE_PAGE       0x80

#define PG_STRONG_UC (PG_CACHE_DISABLE | PG_WRITE_THROUGH)

void *get_physaddr(void *virtualaddr);
int map_pages(void *physaddr, void *virtualaddr, u16 flags, int num_pages);
int unmap_pages(void *virtualaddr, int num_pages);

static inline int map_page(void *physaddr, void *virtualaddr, u16 flags)
{
    return map_pages(physaddr, virtualaddr, flags, 1);
}

static inline int unmap_page(void *virtualaddr)
{
    return unmap_pages(virtualaddr, 1);
}

void x86_setup_mem(void);
void init_phys_mem_mapping(size_t memory_sz_kb);

#ifdef __x86_64__
pdpte_t * get_or_alloc_pdpt(pml4e_t *pml4, void *virt, u16 flags);
#endif
pde_t * get_or_alloc_pd(pdpte_t *pdpt, void *virt, u16 flags);
pte_t * get_or_alloc_pt(pde_t *pd, void *virt, u16 flags);

#endif
