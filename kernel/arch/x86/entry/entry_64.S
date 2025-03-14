// Copyright (C) 2025 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#define __ASSEMBLY__
#include <asm/segments.h>

#include "macros.h"

.equ TASK_PGD_OFFSET,    16
.equ TASK_PC_OFFSET,     24
.equ TASK_KSTACK_OFFSET, 32

.text
// void jump_usermode(void *entry, void *ustack, void *kstack)
// rdi = entry, rsi = ustack, rdx = kstack
.globl jump_usermode
.type jump_usermode, @function
jump_usermode:
	cli
	push %rbp
	mov  %rsp, %rbp
	push %rdi
	mov  %rdx, %rdi
	call set_tss_esp0
	pop  %rdi
	movq $(__USER_DS | 0x3), %rax 	# ring 3 data
	mov  %ax, %ds
	mov  %ax, %es
	mov  %ax, %fs
	mov  %ax, %gs

	# set up iret stack frame
	push %rax 			 	# ss
	push %rsi 			 	# esp
	pushfq
	mov  (%rsp), %rax
	or   $0x200, %rax
	mov  %rax, (%rsp)	 	# eflags
	pushq $__USER_CS | 0x3 	# cs (ring 3)
	push %rdi			 	# eip

	CLEAR_REGS

	iretq

// void arch_context_switch(struct task *prev, struct task *next)
.globl arch_context_switch
arch_context_switch:
	push   	%rbp
	mov		%rsp, %rbp
	push 	%r15
	push 	%r14
	push 	%r13
	push 	%r12
	push  	%rbx

	# Stack:
	# next 24(%bp)
	# prev 16(%bp)
	# frame p
	# ret

	mov 	0x10(%rbp), %rsi
	mov 	0x18(%rbp), %rbx
	// leal   	0x3e(%ebx), %edx
	// leal    0x3e(%esi), %eax
/*
	pushl   $0x0
	pushl   %edx
	pushl  	%eax
	pushl	$.ct_string
	pushl  	$0x0
	call	klog
	addl    $0x14, %esp
*/
	mov 	TASK_KSTACK_OFFSET(%rbx), %rdi
	call  	set_tss_esp0

	pushf
	mov 	%rsp, TASK_KSTACK_OFFSET(%rsi)
	mov 	TASK_PGD_OFFSET(%rbx), %rax
	mov 	%rax, %cr3
	mov 	TASK_KSTACK_OFFSET(%rbx), %rsp
	movq 	$1f, TASK_PC_OFFSET(%rsi)
	push 	TASK_PC_OFFSET(%rbx)
	ret

1:	popf
	pop 	%rbx
	pop 	%r12
	pop 	%r13
	pop 	%r14
	pop 	%r15
	pop 	%rbp
	ret

.section .rodata
.ct_string:
	.asciz "Context switch from %s to %s\n"

/*
int arch_return_from_fork(void *regs);
	Params:
	rdi = ptr to regs

	Jump to usermode in new process.
*/
.text
.globl arch_return_from_fork
.type arch_return_from_fork, @function
arch_return_from_fork:
	mov %rdi, %rax

	RESTORE_REGS

	SET_IRET_STACK

	# return to usermode
	xor %rax, %rax	# return 0 to child
	iretq
