#ifndef KERNEL_MM_H
#define KERNEL_MM_H

#include <kernel/types.h>

#define K_WRITE 0x3
#define K_RO    0x1
#define U_WRITE 0x7
#define U_RO    0x5

void mm_init(void);
void* kvirtual_alloc(unsigned int num_pages, int flags);
void kvirtual_free(void* addr, unsigned int num_pages);

#endif
