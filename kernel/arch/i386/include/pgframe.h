#ifndef KERNEL_PGFRAME_H
#define KERNEL_PGFRAME_H

#include <kernel/types.h>

int phys_mem_init(u32 mem_end);
void *alloc_frames(u32 num_pages);
void free_frames(void *frame, u32 num_pages);

static inline void *alloc_frame() 
{
    return alloc_frames(1);
}

static inline void free_frame(void *frame) 
{
    free_frames(frame, 1);
}

#endif
