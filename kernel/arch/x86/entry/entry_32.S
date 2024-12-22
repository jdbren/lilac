// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <asm/segments.h>

.globl jump_usermode
.type jump_usermode, @function
jump_usermode:
	cli
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	pushl %esi
	pushl %edi
	pushl %esp
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

	# Clear registers
	xorl %eax, %eax
	xorl %ebx, %ebx
	xorl %ecx, %ecx
	xorl %edx, %edx
	xorl %esi, %esi
	xorl %edi, %edi
	xorl %ebp, %ebp

	iret

// void arch_context_switch(struct task *prev, struct task *next)
.globl arch_context_switch
arch_context_switch:
	pushl   %ebp
	movl 	%esp, %ebp
	pushl   %esi
	pushl  	%ebx

	# Stack:
	# next 12(%ebp)
	# prev 8(%ebp)
	# ret
	# frame = ebp

	movl   	0x08(%ebp), %esi
	movl   	0x0c(%ebp), %ebx
	leal   	0x3e(%ebx), %edx
	leal    0x3e(%esi), %eax

	pushl   $0x0
	pushl   %edx
	pushl  	%eax
	pushl	$.ct_string
	pushl  	$0x0
	call	klog
	addl    $0x14, %esp

	pushl   0x10(%ebx)
	call  	set_tss_esp0
	addl    $0x4, %esp

	pushf
	movl    %esp, 0x10(%esi)
	movl    0x8(%ebx), %eax
	movl    %eax, %cr3
	movl    0x10(%ebx), %esp
	movl   	$1f, 0xc(%esi)
	pushl   0xc(%ebx)
	ret

1:	popf
	add    $0x10, %esp
	leal   -0x8(%ebp),%esp
	pop    %ebx
	pop    %esi
	pop    %ebp
	ret

.ct_string:
	.asciz "Context switch from %s to %s\n"
