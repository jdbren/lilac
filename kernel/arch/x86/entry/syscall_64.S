// Copyright (C) 2025 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#define __ASSEMBLY__
#include <asm/segments.h>
#include <asm/gdt.h>

#include "macros.S"

.text
.globl syscall_handler
syscall_handler:
	# already on stack: ss, sp, flags, cs, ip.
	PUSH_REGS

	# set up syscall
	mov  RSI(%rsp), %rcx

	lea  syscall_table, %rbp
	mov  (%rbp, %rax, 8), %rax
	xor  %rbp, %rbp
	call *%rax
	# restore registers
	POP_REGS pop_rax=0
	iretq

/*
 * Entry point using the syscall instruction
 * rip saved in rcx, rflags in r11
 * most flags cleared including IF and DF
 * Args from user: rdi, rsi, rdx, r10, r8, r9
 */
.globl syscall_entry
syscall_entry:
	swapgs
	mov %rsp, %gs:TSS_rsp2 # save user stack in scratch
	mov %gs:TSS_rsp0, %rsp # switch to kernel stack

	# set up regs struct
	push $__USER_DS   # ss
	push %gs:TSS_rsp2 # user stack
	push %r11         # rflags
	push $__USER_CS   # cs
	push %rcx         # rip
	PUSH_REGS

	# set up syscall
	mov  R10(%rsp), %rcx # C ABI expects 4th arg in rcx
	lea  syscall_table, %rbp
	mov  (%rbp, %rax, 8), %rax
	xor  %rbp, %rbp
	call *%rax
	# restore registers
	POP_REGS pop_rax=0
	add $40, %rsp # remove ss, sp, flags, cs, ip since using sysret

	# prepare exit
	mov %gs:TSS_rsp2, %rsp # switch back to user stack
	swapgs
	sysretq


.globl sys_fork
sys_fork:
	lea  8(%rsp), %rdi
	call arch_do_fork
	ret
