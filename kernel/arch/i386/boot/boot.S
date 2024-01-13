#define __PAGE_OFFSET 4096
#define __KERNEL_BASE 0xC0000000
#define pa(X) ((X) - __KERNEL_BASE)

#define __BOOT_CS 0x08
#define __BOOT_DS 0x10

# Declare constants for the multiboot header.
.set ALIGN,    1<<0             # align loaded modules on page boundaries
.set MEMINFO,  1<<1             # provide memory map
.set FLAGS,    ALIGN | MEMINFO  # this is the Multiboot 'flag' field
.set MAGIC,    0x1BADB002       # 'magic number' lets bootloader find the header
.set CHECKSUM, -(MAGIC + FLAGS) # checksum of above, to prove we are multiboot

# Declare a multiboot header that marks the program as a kernel.
.section .multiboot.data, "aw"
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

# Allocate the initial stack and page tables.
.section .bsstack, "aw", @nobits
.align 4096
stack_bottom:
	.skip 16384
stack_top:

.section .bss, "aw", @nobits
.align __PAGE_OFFSET
.globl page_directory
page_directory:
	.fill 1024,4,0
boot_page_table0:
	.fill 1024,4,0
boot_page_table768:
	.fill 1024,4,0

# The kernel entry point.
.section .multiboot.text, "a"
.global _start
.type _start, @function
_start:
	movl pa(stack_top), %ecx

	# Set up the boot segment registers.
	lgdt pa(boot_gdt_descr)
	movl $(__BOOT_DS),%eax
	movl %eax,%ds
	movl %eax,%es
	movl %eax,%fs
	movl %eax,%gs
	movl %eax,%ss
	leal -__KERNEL_BASE(%ecx), %esp
	cld

	# Identity map the kernel.
	movl $pa(boot_page_table0), %edi # page table entry
	xorl %eax, %eax				 # start addr = 0

1:	movl %eax, %esi
	orl	 $3, %esi				# set present bit and r/w
	movl %esi, (%edi)			# set page table entry
	addl $4, %edi				# next page table entry
	addl $4096, %eax			# next page address
	cmpl $(pa(_kernel_end)), %eax		# check if we are done
	jne  1b 					# if not, continue

	# Set up the kernel page mapping (higher half).
	movl $0, %eax
	movl $(pa(boot_page_table768)), %edi # addresses 0xC0000000...kernel end
	movl $pa(_kernel_end), %ecx

2:	movl %eax, %esi
	orl	 $3, %esi
	movl %esi, (%edi)
	addl $4, %edi
	addl $4096, %eax
	cmpl %eax, %ecx
	jne  2b

	# Set up the page directory.
	movl $pa(page_directory), %edi
	movl $pa(boot_page_table0), %esi
	orl  $3, %esi
	movl %esi, (%edi)

	movl $pa(boot_page_table768), %esi
	orl  $3, %esi
	addl $(4*768), %edi
	movl %esi, (%edi)

	# Recursive map
	movl $pa(page_directory), %eax
	movl %eax, %edi
	addl $(4*1023), %edi
	orl  $3, %eax
	movl %eax, (%edi)

	# Enable paging.
	movl $pa(page_directory), %eax
	movl %eax, %cr3
	movl %cr0, %eax
	orl  $(0x80010000), %eax # pg enable, write-protect enable
	movl %eax, %cr0

	# Switch to the higher half.
	ljmp $__BOOT_CS,$3f
	

.section .text
3: 	movl $(boot_page_table0 + 1024), %edi

	# Unmap the kernel identity mapping as it is now unnecessary.
4:	movl $0, (%edi)
	addl $4, %edi
	cmpl $(boot_page_table0 + 4096), %edi
	jne  4b

	# Reload cr3 to force a TLB flush so the changes take effect.
	movl %cr3, %ecx
	movl %ecx, %cr3

	# Set up the stack.
	movl $stack_top, %esp

	call _init

	call kernel_main

	# Infinite loop if the system has nothing more to do.
	cli
1:	hlt
	jmp 1b

.section .data
.align 4
.word 0
boot_gdt_descr:
	.word 0xbf
	.long boot_gdt - __KERNEL_BASE

boot_gdt:
	.quad 0x0000000000000000	# null descriptor
	.quad 0x00CF9A000000FFFF	# code descriptor
	.quad 0x00CF92000000FFFF	# data descriptor
