// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _KERNEL_LILAC_H
#define _KERNEL_LILAC_H

#include <utility/multiboot2.h>

struct multiboot_info {
	char *cmdline;
	char *bootloader;
	struct multiboot_tag_basic_meminfo *meminfo;
	struct multiboot_tag_bootdev *boot_dev;
	struct multiboot_tag_mmap *mmap;
	union {
		struct multiboot_tag_old_acpi *old_acpi;
		struct multiboot_tag_new_acpi *new_acpi;
	};
    struct multiboot_tag_efi32 *efi32;
	struct multiboot_tag_efi_mmap *efi_mmap;
	struct multiboot_tag_load_base_addr *base_addr;
};

void start_kernel(void);
void *get_rsdp();

#endif
