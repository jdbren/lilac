#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#include <stdio.h>

#define assert(x) if (!(x)) kerror("Assertion failed: " #x)

static inline void kerror(const char* msg) {
	printf("Kernel panic: %s\n", msg);
	while (1) {
		asm("hlt");
	}
}

#endif
