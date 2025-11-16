// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef KERNEL_MM_H
#define KERNEL_MM_H

#include <lilac/types.h>
#include <lilac/config.h>
#include <utility/multiboot2.h>

#define MEM_WRITE           0001
#define MEM_USER            0002
#define MEM_NO_EXEC         0004
#define MEM_GLOBAL          0010
#define MEM_CACHE_WT        0020
#define MEM_NO_CACHE        0040
#define MEM_UC              0100

#define PAGE_ROUND_DOWN(x)  (((uintptr_t)(x)) & (~(PAGE_SIZE-1)))
#define PAGE_ROUND_UP(x)    ((((uintptr_t)(x)) + PAGE_SIZE-1) & (~(PAGE_SIZE-1)))

#define PAGE_UP_COUNT(x)    (PAGE_ROUND_UP(x) / PAGE_SIZE)
#define PAGE_DOWN_COUNT(x)  (PAGE_ROUND_DOWN(x) / PAGE_SIZE)

void mm_init(void);
int kernel_pt_init(uintptr_t start, uintptr_t end);

// void *kvirtual_alloc(int size, int flags);
// void kvirtual_free(void* addr, int size);
void * get_free_vaddr(int num_pages);

int map_to_self(void* addr, int size, int flags);
void unmap_from_self(void* addr, int size);

void *map_phys(void *phys, int size, int flags);
void unmap_phys(void *addr, int size);

void *map_virt(void *virt, int size, int flags);
void unmap_virt(void *virt, int size);

// Architecture specific implementations

uintptr_t arch_get_pgd(void);
void copy_kernel_mappings(uintptr_t phys_cr3);

uintptr_t __walk_pages(void *vaddr);

int map_pages(void *physaddr, void *virtualaddr, int flags, int num_pages);
int unmap_pages(void *virtualaddr, int num_pages);

static inline int map_page(void *physaddr, void *virtualaddr, int flags)
{
    return map_pages(physaddr, virtualaddr, flags, 1);
}

static inline int unmap_page(void *virtualaddr)
{
    return unmap_pages(virtualaddr, 1);
}

#endif
