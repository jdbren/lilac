#ifndef KERNEL_PAGING_H
#define KERNEL_PAGING_H

void map_page(void *physaddr, void *virtualaddr, unsigned int flags);
void unmap_page(void *virtualaddr);

#endif
