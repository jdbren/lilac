// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#include <lilac/log.h>

void print_bitmap(void);

#ifdef DEBUG
#define assert(x) if (unlikely(!(x))) kerror("Assertion failed: " #x)
#else
#define assert(x) ((void)0)
#endif

[[noreturn]] static inline void kerror(const char *msg) {
	klog(LOG_FATAL, "Kernel panic: %s\n", msg);
	// print_bitmap();
	asm("cli");
	while (1)
		asm("hlt");
}

#endif
