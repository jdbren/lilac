#ifndef KERNEL_MM_H
#define KERNEL_MM_H

#include <kernel/types.h>
#include <utility/multiboot2.h>

#define K_WRITE 0x3
#define K_RO    0x1
#define U_WRITE 0x7
#define U_RO    0x5

void mm_init(struct multiboot_tag_mmap *mmap, u32 mem_upper);
void* kvirtual_alloc(int size, int flags);
void kvirtual_free(void* addr, int size);
int map_to_self(void* addr, int flags);

#endif
