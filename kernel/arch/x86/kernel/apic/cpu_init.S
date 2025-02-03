#include <lilac/config.h>

# TODO: Set up acutal stack and code to jump to

# this code will be relocated to 0x8000, sets up environment for calling a C function
.globl  ap_tramp
.code16
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
	.long 0x00000068, 0x00CF8900    # tss
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
	movl	$0x1000, %eax
	mul		%ebx
	addl	$pa(ap_stack_top), %eax
	mov		%eax, %esp
	push	%ebx
	# spinlock, wait for the BSP to finish
1:  pause
	cmpb	$0, pa(bspdone)
	jz		1b
	lock	incb pa(aprunning)
	# jump into C code (should never return)
	ljmp	$0x8, $pa(ap_startup)

.align 32
ap_stack_top:
	.fill 0x8000, 1, 0
