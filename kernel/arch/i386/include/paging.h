#ifndef x86_PAGING_H
#define x86_PAGING_H

#include <kernel/types.h>

#define PAGE_BYTES 4096

#define PG_READ            0x0
#define PG_WRITE           0x2
#define PG_SUPER           0x0
#define PG_USER            0x4
#define PG_WRITE_THROUGH   0x8
#define PG_CACHE_DISABLE   0x10

void* get_physaddr(void *virtualaddr);
int map_pages(void *physaddr, void *virtualaddr, u16 flags, int num_pages);
int unmap_pages(void *virtualaddr, int num_pages);

inline int map_page(void *physaddr, void *virtualaddr, u16 flags)
{
    return map_pages(physaddr, virtualaddr, flags, 1);
}

inline int unmap_page(void *virtualaddr)
{
    return unmap_pages(virtualaddr, 1);
}

#endif
