// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef KERNEL_HEAP_H
#define KERNEL_HEAP_H

#include <lilac/types.h>

struct kmalloc_stats {
	unsigned long small_pages;
	unsigned long cached_small_pages;
	unsigned long large_pages;
	unsigned long total_pages;
};

void kfree(const void *addr);

[[gnu::malloc(kfree)]] void* kmalloc(size_t size);
[[gnu::malloc(kfree)]] void* kzmalloc(size_t size);
[[gnu::malloc(kfree)]] void* kcalloc(size_t num, size_t size);
void* krealloc(void *addr, size_t size);
void kmalloc_get_stats(struct kmalloc_stats *stats);
void print_kmalloc_stats(void);

#endif
