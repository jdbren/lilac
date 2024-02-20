#ifndef _ASM_MSR_H
#define _ASM_MSR_H

#include <stdbool.h>
#include <kernel/types.h>
#include <asm/cpuid.h>

static const u32 CPUID_FLAG_MSR = 1 << 5;

inline static bool cpuHasMSR()
{
    u32 a, d, b, c;
    cpuid(1, &a, &b, &c, &d);
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
