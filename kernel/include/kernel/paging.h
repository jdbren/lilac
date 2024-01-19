#ifndef KERNEL_PAGING_H
#define KERNEL_PAGING_H

#include <stdint.h>

#define PAGE_BYTES 4096

int map_page(void *physaddr, void *virtualaddr, uint16_t flags);
int unmap_page(void *virtualaddr);

#endif
