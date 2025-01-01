// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef KERNEL_HEAP_H
#define KERNEL_HEAP_H

#include <lilac/types.h>

void* kmalloc(size_t size);
void* kzmalloc(size_t size);
void* krealloc(void *addr, size_t size);
void* kcalloc(size_t num, size_t size);
void  kfree(void *addr);

#endif
