#ifndef __ASM_X86_MACROS_H
#define __ASM_X86_MACROS_H

#ifndef __ASSEMBLY__
#error "This file is not supposed to be included in C files"
#endif

#include <asm/regs.h>

.macro ret_from_irq
#ifdef __x86_64__
	iretq
#else
	iret
#endif
.endm

#ifdef __x86_64__
.macro PUSH_REGS rdx=%rdx rcx=%rcx rax=%rax
	pushq \rax
	pushq %rbp
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15
	pushq %rdi
	pushq %rsi
	pushq \rdx
	pushq \rcx
	pushq %rbx
.endm

.macro CLEAR_REGS
	xorq %r15, %r15
	xorq %r14, %r14
	xorq %r13, %r13
	xorq %r12, %r12
	xorq %rbp, %rbp
	xorq %rbx, %rbx
	xorq %r11, %r11
	xorq %r10, %r10
	xorq %r9,  %r9
	xorq %r8,  %r8
	xorq %rax, %rax
	xorq %rcx, %rcx
	xorq %rdx, %rdx
	xorq %rsi, %rsi
	xorq %rdi, %rdi
.endm

.macro PUSH_AND_CLEAR_REGS
	PUSH_REGS
	CLEAR_REGS
.endm

.macro POP_REGS pop_rax=1
	popq %rbx
	popq %rcx
	popq %rdx
	popq %rsi
	popq %rdi
	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rbp
	.if \pop_rax
	popq %rax
	.else
	addq $8, %rsp
	.endif
.endm

.macro RESTORE_REGS
	movq RBX(%rax), %rbx
	movq RCX(%rax), %rdx
	movq RDX(%rax), %rcx
	movq RSI(%rax), %rsi
	movq RDI(%rax), %rdi
	movq R15(%rax), %r15
	movq R14(%rax), %r14
	movq R13(%rax), %r13
	movq R12(%rax), %r12
	movq R11(%rax), %r11
	movq R10(%rax), %r10
	movq R9(%rax),  %r9
	movq R8(%rax),  %r8
	movq RBP(%rax), %rbp
.endm

.macro SET_IRET_STACK
	pushq SS(%rax)
	pushq RSP(%rax)
	pushq RFLAGS(%rax)
	pushq CS(%rax)
	pushq RIP(%rax)
.endm

.macro EXIT_CHECK_AND_SWAPGS

.endm

.macro TEST_USER_CS
	testb $3, CS(%rsp)
.endm

#else /* 32-bit: */
.macro PUSH_REGS
	pushl %gs
	pushl %fs
	pushl %es
	pushl %ds
	pushl %eax
	pushl %ebp
	pushl %edi
	pushl %esi
	pushl %edx
	pushl %ecx
	pushl %ebx
.endm

.macro CLEAR_REGS
	xor %ebp, %ebp
	xor %ebx, %ebx
	xor %eax, %eax
	xor %ecx, %ecx
	xor %edx, %edx
	xor %esi, %esi
	xor %edi, %edi
.endm

.macro PUSH_AND_CLEAR_REGS
	PUSH_REGS
	CLEAR_REGS
.endm

.macro POP_REGS pop_eax=1
	popl %ebx
	popl %ecx
	popl %edx
	popl %esi
	popl %edi
	popl %ebp
	.if \pop_eax
	popl %eax
	.else
	addl $4, %esp
	.endif
	popl %ds
	popl %es
	popl %fs
	popl %gs
.endm

.macro RESTORE_REGS
	movl EBX(%eax), %ebx
	movl ECX(%eax), %ecx
	movl EDX(%eax), %edx
	movl ESI(%eax), %esi
	movl EDI(%eax), %edi
	movl EBP(%eax), %ebp
	movw DS(%eax), %ds
	movw ES(%eax), %es
	movw FS(%eax), %fs
	movw GS(%eax), %gs
.endm

.macro SET_IRET_STACK
	pushl SS(%eax)
	pushl ESP(%eax)
	pushl EFLAGS(%eax)
	pushl CS(%eax)
	pushl EIP(%eax)
.endm

.macro TEST_USER_CS
	testb $3, CS(%esp)
.endm
#endif /* __x86_64__ */

#endif /* __ASM_X86_MACROS_H */
