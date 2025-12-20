#define __ASSEMBLY__
#include <lilac/config.h>
#include <asm/segments.h>
#include <asm/cpu-flags.h>
#include <asm/msr.h>


# this code will be relocated to 0x8000, sets up environment for calling a C function
.globl  ap_tramp
.code16
.org 0x8000
ap_tramp:
	cli
	cld
	ljmp $0, $0x8040
.align 16
.org 0x8010
_L8010_GDT_table:
	.quad 0x0000000000000000	    # null descriptor
	.quad 0x00CF9A000000FFFF	    # code descriptor
	.quad 0x00CF92000000FFFF	    # data descriptor
	.quad 0x00AF9A000000FFFF    	# Kernel 64 bit code
.org 0x8030
_L8030_GDT_value:
	.word _L8030_GDT_value - _L8010_GDT_table - 1
	.long 0x8010
	.long 0, 0
.align 64
.org 0x8040
_L8040:
	xorw	%ax, %ax
	movw	%ax, %ds
	lgdt	0x8030
	movl	%cr0, %eax
	orl		$1, %eax
	movl	%eax, %cr0
	ljmp	$8, $0x8060
.align 32
.code32
.org 0x8060
_L8060:
	movw	$0x10, %ax
	movw	%ax, %ds
	movw	%ax, %ss
	# get Local APIC ID
	mov		$1, %eax
	cpuid
	shrl	$24, %ebx
	# set up a stack
	movl	$__KERNEL_STACK_SZ, %eax
	mul		%ebx
	addl	$__pa(ap_stack), %eax
	mov		%eax, %esp
	push	%ebx
	# spinlock, wait for the BSP to finish
1:  pause
	cmpb		$0, __pa(bspdone)
	jz			1b
	lock incb 	__pa(aprunning)

#ifdef __x86_64__
init_cpu:
	# prepare jump into C code (should never return)
	mov  $(X86_CR4_PAE|X86_CR4_PGE|X86_CR4_MCE|X86_CR4_OSFXSR|X86_CR4_FSGSBASE), %eax
	mov  %eax, %cr4

	mov  $__pa(boot_pml4), %eax
	mov  %eax, %cr3

	# Set EFER
	movl $IA32_EFER, %ecx
	movl $(EFER_LME | EFER_SCE | EFER_NXE), %eax
	xorl %edx, %edx
	wrmsr

	mov $(X86_CR0_PE|X86_CR0_PG|X86_CR0_WP|X86_CR0_AM \
		|X86_CR0_NE|X86_CR0_ET|X86_CR0_MP), %eax
	mov %eax, %cr0

	ljmp  $24,$0x8100
.code64
.align 0x100
.org 0x8100
	lgdt gdt
	mov $__KERNEL_DS, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss
	add $__KERNEL_BASE, %rsp
	pushq $__KERNEL_CS
	pushq $._high
	lretq
._high:
	call ap_startup
#else // 32-bit mode
init_cpu:
	# prepare jump into C code (should never return)
	mov  $(X86_CR4_OSFXSR|X86_CR4_PSE|X86_CR4_MCE|X86_CR4_PGE), %eax
	mov  %eax, %cr4

	mov  $__pa(page_directory), %eax
	mov  %eax, %cr3

	mov $(X86_CR0_PE|X86_CR0_PG|X86_CR0_WP|X86_CR0_AM \
		|X86_CR0_NE|X86_CR0_ET|X86_CR0_MP), %eax
	mov %eax, %cr0

	lgdt gdt
	mov $__KERNEL_DS, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss
	add $__KERNEL_BASE, %esp
	ljmp $__KERNEL_CS,$ap_startup
#endif
