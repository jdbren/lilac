// Copyright (C) 2025 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/config.h>
#include <asm/segments.h>

.text
.globl startup_64
startup_64:
.code32
	cli
	hlt

.global gdt_init
.type gdt_init, @function
gdt_init:
	leaq gdt_descr, %rax
	movq $gdt_entries, 2(%eax)
	lgdt (%eax)
	call tss_init
	ret

.get_tss:
	movl $1, %eax
	cpuid
	shrl $24, %ebx
	leal tss, %ecx
	movl $0x68, %eax
	mul  %ebx
	addl %ecx, %eax
	ret

tss_init:
	push %ebp
	mov  %esp, %ebp
	push %ebx

	call .get_tss # eax = &tss, ebx = cpu_id

	leal gdt_entries+40(, %ebx, 8), %edx   # TSS descriptor for this CPU
	movl $0, 4(%eax)
	movl $__KERNEL_DS, 8(%eax)

	addl $2, %edx
	movw %ax, (%edx)

	addl $2, %edx
	shrl $16, %eax
	mov  %al, (%edx)

	addl $3, %edx
	shrl $8, %eax
	mov  %al, (%edx)

	movl $8, %eax
	mulw %bx  		# RPL 0 selector index
	addw $5 * 8, %ax
	ltr  %ax

	pop  %ebx
	pop  %ebp
	ret

.global set_tss_esp0
set_tss_esp0:
	push %ebp
	movl %esp, %ebp
	push %ebx
	call .get_tss
	movl 8(%ebp), %ebx
	movl %ebx, 4(%eax)
	pop %ebx
	pop %ebp
	ret


.data
.align 8
gdt_descr:
	.word 13 * 64 - 1
	.quad gdt_entries

.align 16
gdt_entries:
    # NULL
	.quad 0x0, 0x0
    # Kernel 32 bit code
	.quad 0x00CF9A000000FFFF
    .quad 0x0000000000000000
    # Kernel 64 bit code
    .quad 0x00AF9A000000FFFF
    .quad 0x0000000000000000
    # Kernel data
	.quad 0x00CF92000000FFFF
    .quad 0x0000000000000000
    # User 32 bit code
	.quad 0x00CFFA000000FFFF
    .quad 0x0000000000000000
    # User 64 bit code
    .quad 0x00AFFA000000FFFF
    .quad 0x0000000000000000
    # User data
	.quad 0x00CFF2000000FFFF
    .quad 0x0000000000000000
    # TSS
	.rept 8
	.quad 0x0000890000000000
    .quad 0x0000000000000000
	.endr

.bss
.align 16
tss:
	.rept 8
	.skip 0x68
	.endr
