// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _MSR_H
#define _MSR_H

#include <stdbool.h>
#include <lilac/types.h>
#include <cpuid.h>

#define CPUID_FLAG_MSR (1 << 5)

inline static bool cpuHasMSR()
{
    u32 a, d, b, c;
    __cpuid(1, a, b, c, d);
    return d & CPUID_FLAG_MSR;
}

inline static void cpuGetMSR(u32 msr, u32 *lo, u32 *hi)
{
    asm volatile("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(msr));
}

inline static void cpuSetMSR(u32 msr, u32 lo, u32 hi)
{
    asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

#endif
