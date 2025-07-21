// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#define __ASSEMBLY__
#include <lilac/config.h>
#include <asm/segments.h>
#include <asm/cpu-flags.h>

.text
.globl startup_32
startup_32:
	# Identity map the kernel.
	leal pa(boot_page_table0), %edi		# page table entry
	xorl %eax, %eax				 		# start addr = 0

1:	movl %eax, %esi
	orl	 $3, %esi					# set present and r/w
	movl %esi, (%edi)				# set page table entry
	addl $4, %edi					# next page table entry
	addl $4096, %eax				# next page address
	cmpl $pa(_kernel_end), %eax		# check if we are done
	jne  1b

	# Set up the kernel page mapping (higher half).
	xorl %eax, %eax
	leal pa(boot_page_table768), %edi
	leal pa(_data_start), %ecx

2:	movl %eax, %esi
	orl	 $1, %esi  		# set present bit, read-only (code)
	movl %esi, (%edi)
	addl $4, %edi
	addl $4096, %eax
	cmpl %eax, %ecx
	jne  2b

	leal pa(_kernel_end), %ecx

3:	movl %eax, %esi
	orl	 $3, %esi 		# set present bit and r/w (data)
	movl %esi, (%edi)
	addl $4, %edi
	addl $4096, %eax
	cmpl %eax, %ecx
	jne  3b

	# Set up the page directory.
	leal pa(page_directory), %edi
	leal pa(boot_page_table0), %esi
	orl  $3, %esi
	movl %esi, (%edi)

	leal pa(boot_page_table768), %esi
	orl  $3, %esi
	addl $(4*768), %edi
	movl %esi, (%edi)

	# Recursive map
	leal pa(page_directory), %eax
	movl %eax, %edi
	addl $(4*1023), %edi
	orl  $3, %eax
	movl %eax, (%edi)

	# Enable paging.
	leal pa(page_directory), %eax
	movl %eax, %cr3
	movl %cr0, %eax
	orl  $(X86_CR0_PG|X86_CR0_WP), %eax # pg enable, wp enable
	movl %eax, %cr0

	# Switch to the higher half.
	ljmp $__KERNEL_CS,$4f

	# Unmap the kernel identity mapping as it is now unnecessary.
4: 	leal boot_page_table0+1024, %edi
	movl $(0xc00), %ecx
	xorl %eax, %eax
	shrl $2, %ecx
	rep ; stosl

	# Reload cr3 to force a TLB flush.
	movl %cr3, %ecx
	movl %ecx, %cr3

	# Adjust the stack pointer.
	addl $__KERNEL_BASE, %esp

	call _init

	call kernel_early

5:	hlt
	jmp 5b

.global gdt_init
.type gdt_init, @function
gdt_init:
	leal gdt_descr, %eax
	movl $gdt_entries, 2(%eax)
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
.align 4
.word 0
gdt_descr:
	.word 13 * 64 - 1
	.long gdt_entries

.align 8
gdt_entries:
	.quad 0x0
	.quad 0x00CF9A000000FFFF
	.quad 0x00CF92000000FFFF
	.quad 0x00CFFA000000FFFF
	.quad 0x00CFF2000000FFFF
	.rept 8
	.quad 0x0000890000000067
	.endr


.bss
.align PAGE_SIZE
.global page_directory
page_directory:
	.skip 4096
boot_page_table0:
	.skip 4096
boot_page_table768:
	.skip 4096

.align 16
tss:
	.rept 8
	.skip 0x68
	.endr
