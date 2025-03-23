// Copyright (C) 2025 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#define __ASSEMBLY__
#include <asm/segments.h>
#include <asm/msr.h>

#include "macros.S"

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
	mov  %rdi, %r8		# store entry address
	mov  %rdx, %rdi
	call set_tss_esp0
	mov  $__USER_DS, %ax 	# ring 3 data
	mov  %ax, %ds
	mov  %ax, %es

	# set up iret stack frame
	push %rax 			 	# ss
	push %rsi 			 	# sp
	pushfq
	mov  (%rsp), %rax
	or   $0x200, %rax		# set IF
	mov  %rax, (%rsp)	 	# flags
	pushq $__USER_CS	 	# cs (ring 3)
	push %r8			 	# ip

	movl $IA32_GS_BASE, %ecx
	rdmsr
	testl %edx,%edx
	jns 1f
	swapgs
1:
	CLEAR_REGS
	iretq

// void arch_context_switch(struct task *prev, struct task *next)
// esi = next, rdi = prev
.type arch_context_switch, @function
.globl arch_context_switch
arch_context_switch:
	push %rbp
	mov  %rsp, %rbp
	push %r15
	push %r14
	push %r13
	push %r12
	push %rbx

	mov  %rdi, %r12
	mov  TASK_KSTACK_OFFSET(%rsi), %rdi
	call set_tss_esp0

	pushf
	mov  %rsp, TASK_KSTACK_OFFSET(%r12)
	mov  TASK_PGD_OFFSET(%rsi), %rax
	mov  %rax, %cr3
	mov  TASK_KSTACK_OFFSET(%rsi), %rsp
	movq $1f, TASK_PC_OFFSET(%r12)
	push TASK_PC_OFFSET(%rsi)
	ret

1:	popf
	pop  %rbx
	pop  %r12
	pop  %r13
	pop  %r14
	pop  %r15
	pop  %rbp
	ret

.section .rodata
.ct_string:
	.asciz "Context switch from %s to %s\n"

/*
int arch_return_from_fork(void *regs, void *kstack);
	Params:
	rdi = ptr to regs
	rsi = ptr to kstack

	Jump to usermode in new process.
*/
.text
.globl arch_return_from_fork
.type arch_return_from_fork, @function
arch_return_from_fork:
	mov %rdi, %r8
	mov %rsi, %rdi
	call set_tss_esp0

	movl $IA32_GS_BASE, %ecx
	rdmsr
	testl %edx,%edx
	jns 1f
	swapgs
1:
	mov %r8, %rax
	RESTORE_REGS

	SET_IRET_STACK

	# return to usermode
	xor %rax, %rax	# return 0 to child
	iretq

.globl jump_new_proc
.type jump_new_proc, @function
jump_new_proc:
	mov  %rdi, %r8
	mov  TASK_KSTACK_OFFSET(%rdi), %rdi
	call set_tss_esp0

	mov  TASK_PGD_OFFSET(%r8), %rax
	mov  %rax, %cr3
	mov  TASK_KSTACK_OFFSET(%r8), %rsp
	push TASK_PC_OFFSET(%r8)
	ret
