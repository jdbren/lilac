// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#include <lilac/log.h>
#include <stdio.h>
#include <stdint.h>

#define assert(x) if (!(x)) kerror("Assertion failed: " #x)

static inline __attribute__((noreturn))
void kerror(const char *msg) {
	klog(LOG_FATAL, "Kernel panic: %s\n", msg);
	asm("cli");
	while (1)
		asm("hlt");
}

#endif
