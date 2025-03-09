// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/config.h>
#include <asm/segments.h>
#define __PAGE_OFFSET PAGE_SIZE

#ifdef ARCH_X86_64

.text
.globl startup_64
startup_64:
.code32
    lgdt pa(boot_gdt_descr)
	movw $__KERNEL_DS, %edx
	movw %dx, %ds
	movw %dx, %es
	movw %dx, %fs
	movw %dx, %gs
	movw %dx, %ss

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

#endif // ARCH_X86_64
