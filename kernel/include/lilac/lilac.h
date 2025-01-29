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

#define __no_ret __attribute__((noreturn))
#define __no_stack_chk __attribute__((no_stack_protector))
#define __always_inline __attribute__((always_inline)) inline


inline void arch_idle(void);
inline void arch_enable_interrupts(void);
inline void arch_disable_interrupts(void);

#ifdef ARCH_x86
static inline void arch_idle(void)
{
    while (1) {
        asm volatile (
            "sti\n\t"
            "hlt"
        );
    }
}

static __always_inline void arch_enable_interrupts(void)
{
    asm volatile ("sti");
}

static __always_inline void arch_disable_interrupts(void)
{
    asm volatile ("cli");
}
#endif

#endif
