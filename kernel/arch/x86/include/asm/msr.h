#ifndef _ASM_MSR_H
#define _ASM_MSR_H

#define IA32_TSC            0x10
#define IA32_PLATFORM_ID    0x17
#define IA32_APIC_BASE      0x1b
#define IA32_TSC_ADJUST     0x3b
#define IA32_MISC_ENABLE    0x1a0
#define IA32_TSC_DEADLINE   0x6e0

#define IA32_SYSENTER_CS    0x174
#define IA32_SYSENTER_ESP   0x175
#define IA32_SYSENTER_EIP   0x176

#define IA32_PAT            0x277

#define IA32_EFER           0xc0000080
#define IA32_STAR           0xc0000081
#define IA32_LSTAR          0xc0000082
#define IA32_CSTAR          0xc0000083
#define IA32_FMASK          0xc0000084
#define IA32_FS_BASE        0xc0000100
#define IA32_GS_BASE        0xc0000101
#define IA32_KERNEL_GS_BASE 0xc0000102
#define IA32_TSC_AUX        0xc0000103

#ifndef __ASSEMBLY__

#include <stdbool.h>
#include <lilac/types.h>
#include <asm/cpuid-bits.h>

inline static bool cpu_has_msr()
{
    u32 a, d, b, c;
    __cpuid(1, a, b, c, d);
    return d & bit_MSR;
}

inline static void read_msr(u32 msr, u32 *lo, u32 *hi)
{
    asm volatile("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(msr));
}

inline static void write_msr(u32 msr, u32 lo, u32 hi)
{
    asm volatile("wrmsr" :: "a"(lo), "d"(hi), "c"(msr));
}

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_MSR_H */
