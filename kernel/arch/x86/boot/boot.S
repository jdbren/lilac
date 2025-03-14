// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#define __ASSEMBLY__ 1
#include <utility/multiboot2.h>
#include <lilac/config.h>
#include <asm/segments.h>

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
.code32
	# Check the magic number.
	cmpl $MULTIBOOT2_BOOTLOADER_MAGIC, %eax
	jne .invlboot
	movl %ebx, %edx
	andl $7, %edx
	jne .invlboot

	leal pa(stack_top), %ecx

	# Set up the boot segment registers.
	lgdt pa(boot_gdt_descr)
	movl $0x10, %edx
	movw %dx, %ds
	movw %dx, %es
	movw %dx, %fs
	movw %dx, %gs
	movw %dx, %ss

	leal (%ecx), %esp

	# Save the multiboot information.
 	movl %ebx, pa(_mbd_addr)
 	leal pa(_mbd_size), %ecx
 	movl (%ebx), %eax
 	movl %eax, (%ecx)

	# Clear the bss.
	cld
	xorl %eax, %eax
	leal pa(_bss_start), %edi
	leal pa(_bss_end), %ecx
	subl %edi, %ecx
	shrl $2, %ecx
	rep ;stosl

	# Copy the multiboot information.
	leal pa(mbinfo), %edi
	movl pa(_mbd_addr), %esi
	movl pa(_mbd_size), %ecx
	rep ;movsb

#ifdef ARCH_x86_64
	movl 	$0x80000000, %eax
    cpuid
    cmpl 	$0x80000001, %eax
    jb 		.invlboot

	movl 	$0x80000001, %eax
    cpuid
    test 	$(1 << 29), %edx
    jz 		.invlboot

	jmp pa(startup_64)
#else
	jmp pa(startup_32)
#endif

.invlboot:
	cli
	hlt

.section .rodata
.align 4
.word 0
boot_gdt_descr:
	.word 0xbf
	.long boot_gdt - __KERNEL_BASE

boot_gdt:
	.quad 0x0000000000000000	# null descriptor
	.quad 0x00CF9A000000FFFF	# code descriptor
	.quad 0x00CF92000000FFFF	# data descriptor

.data
.align 4
_mbd_addr:
	.long 0
_mbd_size:
	.long 0

.bss
.global mbinfo
mbinfo:
	.skip PAGE_SIZE * 4

.section .bsstack, "aw", @nobits
.align PAGE_SIZE
.global stack_top
stack_bottom:
	.skip 0x4000
stack_top:
