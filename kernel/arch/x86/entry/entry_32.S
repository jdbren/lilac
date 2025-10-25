// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#define __ASSEMBLY__
#include <generated/asm-offsets.h>
#include <asm/segments.h>
#include <lilac/syscall.h>
#include <lilac/errno.h>

#include "macros.S"

.text
.globl syscall_handler
syscall_handler:
	# already on stack: ss, sp, flags, cs, ip.
	PUSH_REGS
	movw $__KERNEL_DS, %bp
	movw %bp, %ds
	xor  %ebp, %ebp

	push %esp
	call x86_kernel_entry
	add  $4,%esp

	mov EAX(%esp),%eax
	cmp $MAX_SYSCALL,%eax
	jle .do_syscall
	mov $-ENOSYS,%eax
	jmp .bad_syscall

.do_syscall:
	lea  syscall_table, %ebp
	mov  (%ebp, %eax, 4), %eax
	xor  %ebp, %ebp
	call *%eax
.bad_syscall:
	push %eax
	call x86_kernel_exit
	pop  %eax
	POP_REGS pop_eax=0
	iret


.globl jump_usermode
.type jump_usermode, @function
jump_usermode:
	cli
	pushl %ebp
	movl %esp, %ebp
/*
	Params on stack:
	kernel stack address
	user stack address
	jump address
	return address (no ret)
	pushed bp
*/
	push 16(%ebp)
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

	CLEAR_REGS

	iret

// void __context_switch_asm(struct task *prev, struct task *next)
.globl __context_switch_asm
__context_switch_asm:
	pushl   %ebp
	movl 	%esp, %ebp
	pushl   %edi
	pushl   %esi
	pushl  	%ebx

	# Stack:
	# next 12(%ebp)
	# prev 8(%ebp)
	# ret
	# frame = ebp

	movl   	0x08(%ebp), %esi
	movl   	0x0c(%ebp), %ebx

	pushf
	movl    %esp, TASK_KSTACK_OFFSET(%esi)
	movl    TASK_PGD_OFFSET(%ebx), %eax
	movl    %eax, %cr3
	movl    TASK_KSTACK_OFFSET(%ebx), %esp
	movl    $1f, TASK_PC_OFFSET(%esi)
	pushl   TASK_PC_OFFSET(%ebx)
	ret

1:	popf
	popl    %ebx
	popl    %esi
	popl    %edi
	popl    %ebp
	ret

.section .rodata
.ct_string:
	.asciz "Context switch from %s to %s\n"

/*
int arch_return_from_fork(void *regs, void *kstack);
	Params:
	4(%esp) = ptr to regs
	8(%esp) = kernel stack

	Jump to usermode in new process.
*/
.text
.globl arch_return_from_fork
.type arch_return_from_fork, @function
arch_return_from_fork:
	push %ebp
	mov %esp, %ebp

	push 12(%ebp)
	call set_tss_esp0
	addl $4, %esp

	# restore registers
	movl 8(%ebp), %eax
	RESTORE_REGS

	# restore stack
	SET_IRET_STACK

	# return to usermode
	xor %eax, %eax
	iret

.globl jump_new_proc
.type jump_new_proc, @function
jump_new_proc:
	mov 4(%esp), %eax
	push TASK_KSTACK_OFFSET(%eax)
	call set_tss_esp0
	addl $0x4, %esp

	mov  4(%esp), %esi
	mov  TASK_PGD_OFFSET(%esi), %eax
	mov  %eax, %cr3
	mov  TASK_KSTACK_OFFSET(%esi), %esp
	push TASK_PC_OFFSET(%esi)
	ret
