#ifndef KERNEL_PGFRAME_H
#define KERNEL_PGFRAME_H

#include <stdint.h>

void *alloc_frame(int num_pages);
void free_frame(void *frame, int num_pages);

#endif
