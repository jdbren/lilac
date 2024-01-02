#include <stdio.h>

static inline void kerror(const char* msg) {
	printf("Kernel panic: %s\n", msg);
	while(1) {
		asm("hlt");
	}
}
