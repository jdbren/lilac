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

void mm_init(struct multiboot_tag_efi_mmap *mmap);
void *kvirtual_alloc(int size, int flags);
void kvirtual_free(void* addr, int size);

int map_to_self(void* addr, int flags);
void unmap_from_self(void* addr);

void *map_phys(void *phys, int size, int flags);
void unmap_phys(void *addr, int size);
u32 virt_to_phys(void *vaddr);

u32 arch_get_pgd(void);

#endif
