// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _LILAC_LILAC_H
#define _LILAC_LILAC_H

#include <lilac/types.h>
#include <lilac/boot.h>
#include <acpi/acpi.h>

#define KERNEL_VERSION "0.0.1"

#define __no_ret __attribute__((noreturn))
#define __no_stack_chk __attribute__((no_stack_protector))

struct boot_info {
	struct multiboot_info mbd;
	struct acpi_info acpi;
	struct efi_info efi;
	u64 rsdp;
};

__no_ret __no_stack_chk
void start_kernel(void);
extern inline void arch_idle(void);
extern void arch_enable_interrupts(void);

#endif
