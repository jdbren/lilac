// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#define ASM_FILE 1
#include <utility/multiboot2.h>
#include <lilac/config.h>

# Declare constants for the multiboot2 header.
.set ARCH,     0
.set HEADLEN,  76
.set MAGIC,    MULTIBOOT2_HEADER_MAGIC   # 'magic number' lets bootloader find the header
.set CHECKSUM, -(MAGIC + ARCH + HEADLEN) # checksum of above

# Declare a multiboot2 header.
.section .multiboot.data, "aw"
.align 64
.long MAGIC
.long ARCH
.long HEADLEN
.long CHECKSUM

# Declare the multiboot2 header tags.
.align MULTIBOOT_TAG_ALIGN
.word MULTIBOOT_HEADER_TAG_FRAMEBUFFER
.word 0
.long 20
.long 1280
.long 720
.long 32

.align MULTIBOOT_TAG_ALIGN
.word MULTIBOOT_HEADER_TAG_INFORMATION_REQUEST
.word 0
.long 32 # size
.long MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME
.long MULTIBOOT_TAG_TYPE_BOOTDEV
.long MULTIBOOT_TAG_TYPE_EFI32
.long MULTIBOOT_TAG_TYPE_ACPI_NEW
.long MULTIBOOT_TAG_TYPE_EFI_MMAP
.long MULTIBOOT_TAG_TYPE_EFI32_IH

.align MULTIBOOT_TAG_ALIGN
.word MULTIBOOT_HEADER_TAG_END
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
