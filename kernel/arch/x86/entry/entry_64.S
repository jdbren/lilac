// Copyright (C) 2025 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#define __ASSEMBLY__
#include <asm/segments.h>
#include <asm/msr.h>
#include <asm/gdt.h>
#include <lilac/errno.h>

#include "macros.S"

.equ TASK_PGD_OFFSET,    16
.equ TASK_PC_OFFSET,     24
.equ TASK_KSTACK_OFFSET, 32

.text
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

	mov  %rax, %r12 # save return value
	# reset interrupt stack
	lea  40+15*8(%rsp), %rax
	mov  %rax, %gs:TSS_rsp0

	movl $IA32_GS_BASE, %ecx
	rdmsr
	testl %edx,%edx
	jns 1f
	swapgs
1:
	# restore registers
	mov %r12,%rax # restore return value
	POP_REGS pop_rax=0
	add $40, %rsp # pop manual interrupt stack frame

	# prepare exit
	mov -16(%rsp), %rsp
	sysretq

# unused in 64 bit mode
.globl syscall_handler
syscall_handler:
	mov $-ENOSYS, %rax
	iretq


.globl sys_fork
sys_fork:
	lea  8(%rsp), %rdi
	call arch_do_fork
	ret

// void jump_usermode(void *entry, void *ustack, void *kstack)
// rdi = entry, rsi = ustack, rdx = kstack
.globl jump_usermode
.type jump_usermode, @function
jump_usermode:
	cli
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
	pushf

	mov  %rdi, %r12
	mov  TASK_KSTACK_OFFSET(%rsi), %rdi
	call set_tss_esp0

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
