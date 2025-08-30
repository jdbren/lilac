// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _LILAC_LILAC_H
#define _LILAC_LILAC_H

#include <lilac/config.h>
#include <lilac/err.h>
#include <lilac/log.h>
#include <lilac/panic.h>
#include <lilac/types.h>
#include <lilac/kmalloc.h>

#define KERNEL_VERSION "0.1.0"

#define __no_stack_chk [[gnu::no_stack_protector]]
#ifndef __always_inline
    #define __always_inline [[gnu::always_inline]]
#endif

#if defined __i386__ || defined __x86_64__
static inline void arch_idle(void)
{
    while (1) {
        asm volatile (
            "sti\n\t"
            "hlt"
        );
    }
}

__always_inline
static inline void arch_enable_interrupts(void)
{
    asm volatile ("sti");
}

__always_inline
static inline void arch_disable_interrupts(void)
{
    asm volatile ("cli");
}
#endif // __i386__ || __x86_64__

#endif // _LILAC_LILAC_H
