// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include "kernel/config.h"
#define MB_MAGIC 0x36d76289
#define __PAGE_OFFSET 4096

# Declare constants for the multiboot2 header.
.set ARCH,     0x0
.set HEADLEN,  24
.set MAGIC,    0xE85250D6       # 'magic number' lets bootloader find the header
.set CHECKSUM, -(MAGIC + ARCH + HEADLEN) # checksum of above, to prove we are multiboot

# Declare a multiboot2 header.
.section .multiboot.data, "aw"
.align 64
.long MAGIC
.long ARCH
.long HEADLEN
.long CHECKSUM
.word 0
.word 0
.long 8

# The kernel entry point.
.section .multiboot.text, "a"
.global _start
.type _start, @function
_start:
	# Check the magic number.
	cmpl $MB_MAGIC, %eax
	jne invlboot
	movl %ebx, %edx
	andl $7, %edx
	jne invlboot

	jmp pa(startup_32)

invlboot:
	cli
	hlt
