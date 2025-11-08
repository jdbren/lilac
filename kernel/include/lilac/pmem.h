#ifndef MM_PMEM_H
#define MM_PMEM_H

#include <lilac/lilac.h>
#include <stdatomic.h>

extern volatile u8 *const phys_mem_mapping;

#define get_phys_addr(virt_addr) ((uintptr_t)(virt_addr) - __KERNEL_BASE)

struct frame {
    unsigned long flags;
    atomic_ulong refcount;
    struct list_head cache;
    void *virt;
};

extern struct frame *phys_frames;

void pmem_init(void);
void * arch_map_frame_bitmap(size_t size);

void *alloc_frames(u32 num_pages);
void free_frames(void *frame, u32 num_pages);

static inline void *alloc_frame(void)
{
    return alloc_frames(1);
}

static inline void free_frame(void *frame)
{
    free_frames(frame, 1);
}

#endif
