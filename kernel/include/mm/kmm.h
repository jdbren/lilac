// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef KERNEL_MM_H
#define KERNEL_MM_H

#include <kernel/types.h>
#include <utility/multiboot2.h>

#define PG_READ            0x0
#define PG_WRITE           0x2
#define PG_SUPER           0x0
#define PG_USER            0x4
#define PG_WRITE_THROUGH   0x8
#define PG_CACHE_DISABLE   0x10

void mm_init(struct multiboot_tag_mmap *mmap, u32 mem_upper);
void* kvirtual_alloc(int size, int flags);
void kvirtual_free(void* addr, int size);
int map_to_self(void* addr, int flags);
void *map_phys(void *phys, int size, int flags);
void unmap_phys(void *addr, int num_pages);
u32 virt_to_phys(void *vaddr);

#endif
