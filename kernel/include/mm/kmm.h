#ifndef KERNEL_MM_H
#define KERNEL_MM_H

#include <kernel/types.h>

void mm_init(void);
void* kvirtual_alloc(unsigned int num_pages);
void kvirtual_free(void* addr, unsigned int num_pages);

#endif
