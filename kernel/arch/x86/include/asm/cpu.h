#ifndef X86_ASM_CPU_H
#define X86_ASM_CPU_H

#ifndef __ASSEMBLY__

#include <lilac/types.h>
#include <asm/cpu-features.h>
#include <asm/cpu-flags.h>

extern u32 max_cpuid_leaf;

void x86_memcpy_dwords(void *dst, const void *src, size_t size);
void x86_memcpy_qwords(void *dst, const void *src, size_t size);
void x86_memcpy_sse_nt(void *dst, const void *src, size_t size);

#ifdef __x86_64__
void * x64_memcpy_opt(void *dst, const void *src, size_t size);

#define arch_memcpy_opt(dst, src, size) x86_memcpy_qwords(dst, src, size)
#define memcpy_wc_opt(dst, src, size) x64_memcpy_opt(dst, src, size)
#else
#define arch_memcpy_opt(dst, src, size) x86_memcpy_dwords(dst, src, size)
#define memcpy_wc_opt(dst, src, size) x86_memcpy_dwords(dst, src, size)
#endif

static inline void set_cpuid_max(void)
{
    max_cpuid_leaf = __get_cpuid_max(0, NULL);
}

#endif /* __ASSEMBLY__ */
#endif
