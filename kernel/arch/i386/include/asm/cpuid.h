// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _ASM_CPUID_H
#define _ASM_CPUID_H

#include <kernel/types.h>

#define CPUID_FEAT_EDX_APIC (1 << 9)

inline void cpuid(int code, u32 *a, u32 *b, u32 *c, u32 *d) {
  	asm volatile("cpuid":"=a"(*a),"=b"(*b),"=c"(*c),"=d"(*d):"a"(code));
}

inline int cpuid_string(int code, u32 where[4]) {
  	asm volatile (
		"cpuid" :
		"=a"(*where), "=b"(*(where+1)), "=d"(*(where+2)),"=c"(*(where+3)) :
		"a"(code)
	);
  	return (int)where[0];
}

#endif
