// Copyright (C) 2025 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#define __ASSEMBLY__
#include <asm/segments.h>

#include "macros.h"

.text
.globl syscall_handler
syscall_handler:
	# already on stack: ss, sp, flags, cs, ip.
	PUSH_REGS
    # set up syscall
	lea  syscall_table, %rbp
	mov  (%rbp, %rax, 4), %rax
	xor  %rbp, %rbp
	call *%rax
    # restore registers
	POP_REGS
	iretq

.globl sys_fork
sys_fork:
	lea  8(%rsp), %rdi
	call arch_do_fork
	ret
