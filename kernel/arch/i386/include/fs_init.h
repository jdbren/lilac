#ifndef x86_FS_INIT_H
#define x86_FS_INIT_H

#include <utility/multiboot2.h>

struct multiboot_info {
	char *bootloader;
	struct multiboot_tag_basic_meminfo *meminfo;
	struct multiboot_tag_bootdev *boot_dev;
	struct multiboot_tag_mmap *mmap;
	struct multiboot_tag_old_acpi *acpi;
};

void fs_init(struct multiboot_tag_bootdev* boot_dev);

#endif // FS_INIT_H
