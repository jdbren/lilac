#ifndef KERNEL_ARCH_X86_INCLUDE_ASM_IRQ_H
#define KERNEL_ARCH_X86_INCLUDE_ASM_IRQ_H

#include <lilac/config.h>
#include <x86gprintrin.h>
#include <asm/cpu-flags.h>

#define native_irq_save(flags) asm volatile ("pushf\n\tpop %0" : "=r"(flags) :: "memory")
#define native_irq_restore(flags) asm volatile ("push %0\n\tpopf" :: "r"(flags) : "memory")
#define native_irq_disable() asm volatile ("cli" ::: "memory")
#define native_irq_enable() asm volatile ("sti" ::: "memory")
#define native_cpu_halt() asm volatile ("hlt" ::: "memory")

static inline void arch_idle(void)
{
    while (1) {
        native_irq_enable();
        native_cpu_halt();
    }
}

static __always_inline void arch_enable_interrupts(void)
{
    native_irq_enable();
}

static __always_inline void arch_disable_interrupts(void)
{
    native_irq_disable();
}

static __always_inline void arch_halt_cpu(void)
{
    native_cpu_halt();
}

static inline unsigned long arch_get_flags(void)
{
    unsigned long flags;
    native_irq_save(flags);
    return flags;
}

static inline int arch_irqs_enabled(void)
{
    return (arch_get_flags() & X86_FLAGS_IF) != 0;
}

static inline void arch_local_irq_save(unsigned long *fp)
{
    *fp = arch_get_flags();
}

static inline void arch_local_irq_restore(unsigned long flags)
{
    native_irq_restore(flags);
}

#endif
