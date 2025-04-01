// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#include <lilac/log.h>

#define assert(x) if (unlikely(!(x))) kerror("Assertion failed: " #x)

[[noreturn]] static inline void kerror(const char *msg) {
	klog(LOG_FATAL, "Kernel panic: %s\n", msg);
	asm("cli");
	while (1)
		asm("hlt");
}

#endif
