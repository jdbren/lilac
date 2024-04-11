#ifndef _KERNEL_BOOT_H
#define _KERNEL_BOOT_H

#include <lilac/types.h>
#include <utility/efi.h>
#include <utility/multiboot2.h>

struct multiboot_info {
	char *cmdline;
	char *bootloader;
	struct multiboot_tag_basic_meminfo *meminfo;
	struct multiboot_tag_bootdev *boot_dev;
	struct multiboot_tag_mmap *mmap;
	struct multiboot_tag_framebuffer *framebuffer;
	union {
		struct multiboot_tag_old_acpi *old_acpi;
		struct multiboot_tag_new_acpi *new_acpi;
	};
    struct multiboot_tag_efi32 *efi32;
	struct multiboot_tag_efi_mmap *efi_mmap;
	struct multiboot_tag_load_base_addr *base_addr;
};

struct efi_info {
    efi_table_hdr_t *hdr;
    EFI_RUNTIME_SERVICES *runtime;
    u32 num_tables;
    u32 tables;
};

uintptr_t get_rsdp(void);

#endif
