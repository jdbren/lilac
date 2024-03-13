// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _LILAC_LILAC_H
#define _LILAC_LILAC_H

#include <kernel/types.h>
#include <kernel/boot.h>
#include <acpi/acpi.h>

struct boot_info {
	struct multiboot_info mbd;
	struct acpi_info acpi;
	struct efi_info efi;
	u64 rsdp;
};

void start_kernel(void);

#endif
