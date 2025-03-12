// Copyright (C) 2025 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <asm/segments.h>

.text
.globl syscall_handler
syscall_handler:
	# already on stack: ss, sp, flags, cs, ip.
	pushl %gs
	pushl %fs
	pushl %es
	pushl %ds
	pushl %ebp
	pushl %edi
	pushl %esi
	pushl %edx
	pushl %ecx
	pushl %ebx
	movw $__KERNEL_DS, %bp
	movw %bp, %ds
	leal syscall_table, %ebp
	movl (%ebp, %eax, 4), %eax
	xorl %ebp, %ebp
	calll *%eax
	popl %ebx
	popl %ecx
	popl %edx
	popl %esi
	popl %edi
	popl %ebp
	popl %ds
	popl %es
	popl %fs
	popl %gs
	iret

.globl sys_fork
sys_fork:
	movl %esp, %eax
	addl $4, %eax
	pushl %eax
	# ia32_do_fork(&regs)
	call ia32_do_fork
	addl $4, %esp
	ret
