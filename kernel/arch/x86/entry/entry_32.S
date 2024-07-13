// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <asm/segments.h>

.globl jump_usermode
.type jump_usermode, @function
jump_usermode:
	cli
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	pushl %esi
	pushl %edi
	pushl %esp
	call set_tss_esp0
	addl $4, %esp
	movl 8(%ebp), %ebx 			# return address
	movl 12(%ebp), %edx 		# stack address
	movl $(__USER_DS | 0x3), %eax 	# ring 3 data
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %fs
	movw %ax, %gs

	# set up iret stack frame
	pushl %eax 			 	# ss
	pushl %edx 			 	# esp
	pushf
	movl (%esp), %eax
	orl $0x200, %eax
	mov %eax, (%esp)	 	# eflags
	pushl $__USER_CS | 0x3 	# cs (ring 3)
	pushl %ebx			 	# eip

	# Clear registers
	xorl %eax, %eax
	xorl %ebx, %ebx
	xorl %ecx, %ecx
	xorl %edx, %edx
	xorl %esi, %esi
	xorl %edi, %edi
	xorl %ebp, %ebp

	iret
