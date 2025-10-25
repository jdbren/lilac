#ifndef _ASM_X86_PERCPU_H
#define _ASM_X86_PERCPU_H

#ifdef __ASSEMBLY__
#include <generated/percpu.h>
#else
#include <lilac/percpu.h>

#define CPU_LOCAL_USTACK 	offsetof(struct cpulocal, ustack)
#define CPU_LOCAL_TSS		offsetof(struct cpulocal, priv)

#endif

#endif
