#include "kernel/config.h"
#define __PAGE_OFFSET PAGE_SIZE

#define __BOOT_CS 0x08
#define __BOOT_DS 0x10

.text
.globl startup_32
startup_32:
	leal pa(stack_top), %ecx

	# Set up the boot segment registers.
	lgdt pa(boot_gdt_descr)
	leal (__BOOT_DS), %edx
	movl %edx, %ds
	movl %edx, %es
	movl %edx, %fs
	movl %edx, %gs
	movl %edx, %ss
	leal (%ecx), %esp

    # Save the multiboot information.
 	movl %ebx, pa(_mbd_addr)
 	leal pa(_mbd_size), %ecx
 	movl (%ebx), %eax
 	movl %eax, (%ecx)

	# Clear the bss.
	cld
	xorl %eax, %eax
	leal pa(_bss_start), %edi
	leal pa(_bss_end), %ecx
	subl %edi, %ecx
	shrl $2, %ecx
	rep ; stosl

	# Copy the multiboot information.
	leal pa(mbinfo), %edi
	movl pa(_mbd_addr), %esi
	movl pa(_mbd_size), %ecx
	rep ; movsb

	# Identity map the kernel.
	leal pa(boot_page_table0), %edi 	# page table entry
	xorl %eax, %eax				 		# start addr = 0

1:	movl %eax, %esi
	orl	 $3, %esi					# set present bit and r/w
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
	orl  $(0x80010000), %eax # pg enable, wp enable
	movl %eax, %cr0

	# Switch to the higher half.
	ljmp $__BOOT_CS,$4f
	

    # Unmap the kernel identity mapping as it is now unnecessary.
4: 	leal boot_page_table0+1024, %edi 
	movl $(0xc00), %ecx
	xorl %eax, %eax
	shrl $2, %ecx
	rep ; stosl

	# Reload cr3 to force a TLB flush so the changes take effect.
	movl %cr3, %ecx
	movl %ecx, %cr3

	# Adjust the stack pointer.
	addl $__KERNEL_BASE, %esp

	call _init

	pushl $mbinfo
	call kernel_early

5:	hlt
	jmp 5b

.section .rodata
.align 4
.word 0
boot_gdt_descr:
	.word 0xbf
	.long boot_gdt - __KERNEL_BASE

boot_gdt:
	.quad 0x0000000000000000	# null descriptor
	.quad 0x00CF9A000000FFFF	# code descriptor
	.quad 0x00CF92000000FFFF	# data descriptor

.data
.align 4
_mbd_addr:
	.long 0
_mbd_size:
	.long 0

.bss
.align __PAGE_OFFSET
.global page_directory
.global mbinfo
page_directory:
	.skip 4096
boot_page_table0:
	.skip 4096
boot_page_table768:
	.skip 4096
mbinfo:
	.skip 4096

.section .bsstack, "aw", @nobits
.align 4096
stack_bottom:
	.skip 4096
stack_top:
