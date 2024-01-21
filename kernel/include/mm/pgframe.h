#ifndef KERNEL_PGFRAME_H
#define KERNEL_PGFRAME_H

#include <stdint.h>

void *alloc_frames(uint32_t num_pages);
void free_frames(void *frame, uint32_t num_pages);

static inline void *alloc_frame() 
{
    return alloc_frames(1);
}

static inline void free_frame(void *frame) 
{
    free_frames(frame, 1);
}

#endif
