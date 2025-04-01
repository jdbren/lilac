// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _LILAC_LILAC_H
#define _LILAC_LILAC_H

#include <lilac/config.h>
#include <lilac/err.h>
#include <lilac/log.h>
#include <lilac/panic.h>
#include <lilac/types.h>
#include <lilac/uaccess.h>
#include <mm/kmalloc.h>

#define KERNEL_VERSION "0.1.0"

#define __no_stack_chk [[gnu::no_stack_protector]]
#define __always_inline [[gnu::always_inline]]


#if defined ARCH_x86 || defined ARCH_x86_64
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
#endif // ARCH_x86 || ARCH_x86_64

#endif // _LILAC_LILAC_H
