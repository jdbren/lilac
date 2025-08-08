#ifndef _LILAC_CPUID_H
#define _LILAC_CPUID_H

#include <lilac/types.h>
#include <asm/cpuid-bits.h>

static inline void cpuid(u32 leaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx)
{
    __cpuid(leaf, *eax, *ebx, *ecx, *edx);
#ifdef LOG_CPUID
    klog(LOG_DEBUG, "CPUID leaf 0x%x: eax=0x%x ebx=0x%x ecx=0x%x edx=0x%x\n",
         leaf, *eax, *ebx, *ecx, *edx);
#endif
}

#endif
