// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef KERNEL_HEAP_H
#define KERNEL_HEAP_H

#include <lilac/types.h>

void  kfree(void *addr);

[[gnu::malloc(kfree)]] void* kmalloc(size_t size);
[[gnu::malloc(kfree)]] void* kzmalloc(size_t size);
[[gnu::malloc(kfree)]] void* kcalloc(size_t num, size_t size);
void* krealloc(void *addr, size_t size);

#endif
