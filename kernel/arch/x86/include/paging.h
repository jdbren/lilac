// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef x86_PAGING_H
#define x86_PAGING_H

#include <lilac/types.h>

#define PAGE_BYTES 4096

#define is_aligned(POINTER, BYTE_COUNT) \
    (((uintptr_t)(POINTER)) % (BYTE_COUNT) == 0)

#define PG_DIR_INDEX(x) (((x) >> 22) & 0x3FF)
#define PG_TABLE_INDEX(x) (((x) >> 12) & 0x3FF)

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

int kernel_pt_init(uintptr_t start, uintptr_t end);

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

void init_phys_mem_mapping(size_t memory_sz_kb);

int __map_frame_bm(void *physaddr, void *virtualaddr);

#endif
