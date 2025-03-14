// Copyright (C) 2025 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#define __ASSEMBLY__
#include <asm/segments.h>

#include "macros.h"

.text
.globl syscall_handler
syscall_handler:
	# already on stack: ss, sp, flags, cs, ip.
	# probably don't need to save all optimally but for now entering syscall
	# can lead to context switch so just saving everything
	PUSH_REGS
	movw $__KERNEL_DS, %bp
	movw %bp, %ds
	leal syscall_table, %ebp
	movl (%ebp, %eax, 4), %eax
	xorl %ebp, %ebp
	call *%eax
	POP_REGS pop_eax=0
	iret

.globl sys_fork
sys_fork:
	movl %esp, %eax
	addl $4, %eax
	pushl %eax
	# ia32_do_fork(&regs)
	call arch_do_fork
	addl $4, %esp
	ret
