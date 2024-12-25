// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _LILAC_LILAC_H
#define _LILAC_LILAC_H

#include <lilac/types.h>
#include <lilac/boot.h>
#include <acpi/acpi.h>

#define KERNEL_VERSION "0.1.0"

#define __no_ret __attribute__((noreturn))
#define __no_stack_chk __attribute__((no_stack_protector))
#define __always_inline __attribute__((always_inline)) inline

struct boot_info {
	struct multiboot_info mbd;
	struct acpi_info acpi;
	struct efi_info efi;
	u64 rsdp;
};

__no_ret __no_stack_chk
void start_kernel(void);
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
