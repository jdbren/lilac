#ifndef KERNEL_PAGING_H
#define KERNEL_PAGING_H

#include <stdint.h>

#define PAGE_BYTES 4096

void* get_physaddr(void *virtualaddr);
int map_pages(void *physaddr, void *virtualaddr, uint16_t flags, int num_pages);
int unmap_pages(void *virtualaddr, int num_pages);

inline int map_page(void *physaddr, void *virtualaddr, uint16_t flags) 
{
    return map_pages(physaddr, virtualaddr, flags, 1);
}

inline int unmap_page(void *virtualaddr) 
{
    return unmap_pages(virtualaddr, 1);
}

#endif
