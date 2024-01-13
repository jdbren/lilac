#ifndef KERNEL_MM_H
#define KERNEL_MM_H

#include <stdint.h>

void mm_init();
void* virtual_alloc(int num_pages);
void virtual_free(void* addr, int num_pages);

#endif
