// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#define ASM_FILE 1
#include <utility/multiboot2.h>
#include <kernel/config.h>

# Declare constants for the multiboot2 header.
.set ARCH,     0
.set HEADLEN,  64
.set MAGIC,    MULTIBOOT2_HEADER_MAGIC       # 'magic number' lets bootloader find the header
.set CHECKSUM, -(MAGIC + ARCH + HEADLEN) # checksum of above, to prove we are multiboot

# Declare a multiboot2 header.
.section .multiboot.data, "aw"
.align 64
.long MAGIC
.long ARCH
.long HEADLEN
.long CHECKSUM

# Declare the multiboot2 header tags.
.word MULTIBOOT_HEADER_TAG_INFORMATION_REQUEST
.word 0
.long 40 # size
.long MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME
.long MULTIBOOT_TAG_TYPE_BASIC_MEMINFO
.long MULTIBOOT_TAG_TYPE_BOOTDEV
.long MULTIBOOT_TAG_TYPE_MMAP
.long MULTIBOOT_TAG_TYPE_EFI32
.long MULTIBOOT_TAG_TYPE_ACPI_NEW
.long MULTIBOOT_TAG_TYPE_EFI_MMAP
.long MULTIBOOT_TAG_TYPE_LOAD_BASE_ADDR
# end of tags
.word MULTIBOOT_TAG_TYPE_END
.word 0
.long 8

# The kernel entry point.
.section .multiboot.text, "a"
.global _start
.type _start, @function
_start:
	# Check the magic number.
	cmpl $MULTIBOOT2_BOOTLOADER_MAGIC, %eax
	jne invlboot
	movl %ebx, %edx
	andl $7, %edx
	jne invlboot

	jmp pa(startup_32)

invlboot:
	cli
	hlt
