#ifndef KERNEL_PGFRAME_H
#define KERNEL_PGFRAME_H

#include <stdint.h>

uint32_t* alloc_frame(int num_pages);
void free_frame(uint32_t* frame, int num_pages);

#endif
