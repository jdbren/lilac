// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#define __ASSEMBLY__
#include <asm/msr.h>
#include <asm/regs.h>
#include <asm/segments.h>
#include <asm/gdt.h>
#include <generated/asm-offsets.h>
#include "macros.S"

#ifdef __x86_64__
.macro isr_err name
.extern \name\()_handler
.global \name
\name:
	cld
	testb $3, 16(%rsp)
	jz 1f # if in kernel mode, skip
	swapgs
1:
	popq %gs:CPU_LOCAL_SCRATCH     # store error code
	PUSH_REGS
	xor  %rbp,%rbp

	lea  RIP(%rsp),%rsi  	# interrupt stack frame
	mov  %gs:CPU_LOCAL_SCRATCH,%rdi 	# error code
	call \name\()_handler

	call x86_kernel_exit

	testb $3, CS(%rsp)
	jz 2f # if return to kernel mode, skip
	swapgs
2:
	POP_REGS
	iretq
.endm

.macro isr_no_err name
.extern \name\()_handler
.global \name
\name:
	cld
	testb $3, 8(%rsp)
	jz 1f # if in kernel mode, skip
	swapgs
1:
	PUSH_REGS
	xor  %rbp,%rbp

	lea  RIP(%rsp), %rdi   # interrupt stack frame
	call \name\()_handler

	call x86_kernel_exit

	testb $3, CS(%rsp)
	jz 2f # if return to kernel mode, skip
	swapgs
2:
	POP_REGS
	iretq
.endm
#else // ARCH_x86:
.macro isr_err name
.extern \name\()_handler
.global \name
\name:
	cld
	push %gs
	push %eax
	mov  $__KERNEL_GS, %ax
	mov  %ax, %gs
	mov  8(%esp), %eax
	mov  %eax, %gs:CPU_LOCAL_SCRATCH     # store error code
	pop  %eax
	pop  %gs
	add  $4, %esp # remove error code
	PUSH_REGS

	movw $__KERNEL_GS, %ax
	movw %ax, %gs
	xor  %ebp,%ebp

	lea  EIP(%esp), %eax  # interrupt stack frame
	push %eax
	mov  %gs:CPU_LOCAL_SCRATCH, %eax 	# error code
	push %eax
	call \name\()_handler
	add  $8, %esp

	call x86_kernel_exit

	POP_REGS
	iret
.endm

.macro isr_no_err name
.extern \name\()_handler
.global \name
\name:
	cld
	PUSH_REGS
	xor %ebp,%ebp

	mov $__KERNEL_GS, %ax
	mov %ax, %gs

	lea  EIP(%esp), %eax
	push %eax
	call \name\()_handler
	add  $4, %esp

	call x86_kernel_exit

	POP_REGS
	iret
.endm
#endif /* __x86_64__ */

isr_no_err div0
isr_no_err debug
isr_no_err nmi
isr_no_err brkp
isr_no_err ovflw
isr_no_err bnd
isr_no_err invldop
isr_no_err dna
isr_err    dblflt
# isr_no_err cso
isr_err    invldtss
isr_err    segnp
isr_err    ssflt
isr_err    gpflt
isr_err    pgflt
isr_no_err flpexc
isr_err    align
isr_no_err mchk
isr_no_err simd

# isr_no_err 20
# isr_no_err 21
# isr_no_err 22
# isr_no_err 23
# isr_no_err 24
# isr_no_err 25
# isr_no_err 26
# isr_no_err 27
# isr_no_err 28
# isr_no_err 29
# isr_err    30
# isr_no_err 31

#ifdef __x86_64__
.macro isr_device name func
.global \name
\name:
	cld
	PUSH_REGS
	xor %rbp, %rbp

	testb $3, CS(%rsp)
	jz 1f # if in kernel mode, skip
	swapgs
1:
	mov  %rsp,%rdi
	call x86_kernel_entry

	lea  RIP(%rsp), %rdi   # interrupt stack frame
	call \func
	call apic_eoi

	call x86_kernel_exit

	testb $3, CS(%rsp)
	jz 2f # if return to kernel mode, skip
	swapgs
2:
	POP_REGS
	iretq
.endm
#else
.macro isr_device name func
.global \name
\name:
	cld
	PUSH_REGS
	mov $__KERNEL_GS, %ax
	mov %ax, %gs
	xor %ebp, %ebp

	push %esp
	call x86_kernel_entry
	add $4,%esp

	lea EIP(%esp), %eax
	push %eax
	call \func
	addl $4, %esp

	call apic_eoi
	call x86_kernel_exit

	POP_REGS
	iret
.endm
#endif

.text
isr_device timer_handler timer_tick
isr_device kbd_handler keyboard_int
isr_device serial_handler serial_int
