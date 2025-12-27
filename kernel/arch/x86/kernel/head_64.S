// Copyright (C) 2025 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#define __ASSEMBLY__
#include <lilac/config.h>
#include <asm/segments.h>
#include <asm/msr.h>
#include <asm/cpu-flags.h>

.text
.globl startup_64
startup_64:
.code32
	# Identity map the kernel.
	leal __pa(boot_pt0), %edi		# page table entry
	xorl %eax, %eax				# start addr = 0

1:	movl %eax, %esi
	orl	 $3, %esi					# set present and r/w
	movl %esi, (%edi)				# set page table entry, 32 bit is enough
	addl $8, %edi					# 8 bytes per entry
	addl $4096, %eax				# next page address
	cmpl $__pa(_kernel_end), %eax		# check if we are done
	jne  1b

	# Set up the kernel page mapping (higher half).
	xorl %eax, %eax
	leal __pa(boot_pt0_high), %edi
	leal __pa(_data_start), %ecx

2:	movl %eax, %esi
	orl	 $1, %esi  		# set present bit, read-only (code)
	movl %esi, (%edi)
	addl $8, %edi
	addl $4096, %eax
	cmpl %eax, %ecx
	jne  2b

	leal __pa(_kernel_end), %ecx

3:	movl %eax, %esi
	orl	 $3, %esi 		# set present bit and r/w (data)
	movl %esi, (%edi)
	addl $8, %edi
	addl $4096, %eax
	cmpl %eax, %ecx
	jne  3b

	# Set up the page directory.
	leal __pa(boot_pd), %edi
	leal __pa(boot_pt0), %esi
	orl  $3, %esi
	movl %esi, (%edi)

	leal __pa(boot_pd510_high), %edi
	leal __pa(boot_pt0_high), %esi
	orl  $3, %esi
	movl %esi, (%edi)

	# Set up the page directory pointer table.
	leal __pa(boot_pdtp), %edi
	leal __pa(boot_pd), %esi
	orl  $3, %esi
	movl %esi, (%edi)

	leal __pa(boot_pdtp511_high), %edi
	leal __pa(boot_pd510_high), %esi
	orl  $3, %esi
	movl %esi, 8*510(%edi)

	# Set up the page map level 4.
	leal __pa(boot_pml4), %edi
	leal __pa(boot_pdtp), %esi		 # Map low memory (ident)
	orl  $3, %esi
	# Maintain this identity mapping until the phys_mem_mapping is set up
	movl %esi, (%edi)
	leal __pa(boot_pdtp511_high), %esi # Map high memory
	orl  $3, %esi
	movl %esi, 8*511(%edi)

	# Set CR4 bits and load CR3
	movl %cr4, %eax
	orl  $(X86_CR4_PAE|X86_CR4_PGE|X86_CR4_MCE|X86_CR4_FSGSBASE), %eax
	movl %eax, %cr4
	leal __pa(boot_pml4), %edi
	movl %edi, %cr3

	# Set long mode, syscall, and no-exec
	movl $IA32_EFER, %ecx
	movl $(EFER_LME | EFER_SCE | EFER_NXE), %eax
	xorl %edx, %edx
	wrmsr

	# Enable paging
	movl %cr0, %eax
	orl  $(X86_CR0_PG | X86_CR0_WP), %eax # pg enable, wp enable
	movl %eax, %cr0

	lea  __pa(gdt_descr), %eax
	lgdt (%eax)

	ljmp  $__KERNEL_CS,$__pa(.long_mode)
.code64
.long_mode:
	mov $__KERNEL_DS, %ax
	mov %ax, %ds
	mov %ax, %es
	mov %ax, %fs
	mov %ax, %gs
	mov %ax, %ss
	pushq $__KERNEL_CS
	pushq $.high
	lretq
.high:
	add $__KERNEL_BASE, %rsp
	call gdt_init

	# Initialize static bsp percpu data
	lea cpu_local_storage, %rax
	wrgsbase %rax
/*
	mov $7, %eax				# Enable XCR0.SSE and XCR0.AVX
	xor %edx, %edx
	xor %rcx, %rcx
	xsetbv
*/
	call x86_64_kernel_early

	cli
4:	hlt
	jmp 4b

.global gdt_init
.type gdt_init, @function
gdt_init:
	leaq gdt_descr, %rax
	movq $gdt_entries, 2(%rax)
	lgdt (%rax)
	xor  %rdi, %rdi    # cpu id 0
	call tss_init
	ret

.globl get_tss
.type get_tss, @function
// args: rdi = cpu id
// returns &tss
get_tss:
	movq $0x68, %rax
	mul  %rdi
	lea  tss(%rax), %rax
	ret

.global tss_init
.type tss_init, @function
// args: rdi = cpu id
tss_init:
	push %rbp
	mov  %rsp, %rbp

	call get_tss # rax = &tss

	mov  %rdi, %rdx
    shl  $4, %rdx             # %rdx = %rdx * 16
    add  $gdt_entries+56, %rdx
	movq $0, 4(%rax)

	# 16..31 base
	add  $2, %rdx
	movw %ax, (%rdx)

	# 32..39 base
	add  $2, %rdx
	shr  $16, %rax
	movb  %al, (%rdx)

	# 56..63 base
	add  $3, %rdx
	shr  $8, %rax
	movb  %al, (%rdx)

	# 64..95 base high 32 bits
	add $1, %rdx
	shr $8, %rax
	movl %eax, (%rdx)

	mov  $16, %eax
	mul  %edi  		# RPL 0 selector index
	add  $7 * 8, %ax
	ltr  %ax

	pop  %rbp
	ret

// rdi = stack address
.global set_tss_esp0
set_tss_esp0:
	push %rbp
	mov  %rsp, %rbp
	mov  %rdi, %rcx
	mov  %gs:8, %edi   # cpu id
	call get_tss
	movq %rcx, 4(%rax)
	pop  %rbp
	ret


.data
.align 8
.global gdt
gdt_descr:
gdt:
	.word gdt_entries_end - gdt_entries - 1
	.quad __pa(gdt_entries)

.align 16
gdt_entries:
	.quad 0x0000000000000000	# NULL
	.quad 0x00CF9A000000FFFF    # Kernel 32 bit code
    .quad 0x00AF9A000000FFFF    # Kernel 64 bit code
	.quad 0x00CF92000000FFFF    # Kernel data
	.quad 0x00CFFA000000FFFF    # User 32 bit code
	.quad 0x00CFF2000000FFFF    # User data
	.quad 0x00AFFA000000FFFF    # User 64 bit code
    # TSS
	.rept CONFIG_MAX_CPUS
	.quad 0x0000890000000067
    .quad 0x0000000000000000
	.endr
gdt_entries_end:

.bss
.align PAGE_SIZE
.global boot_pml4
boot_pml4:
	.skip 4096
boot_pdtp:
	.skip 4096
boot_pd:
	.skip 4096
boot_pt0:
	.skip 4096
// boot_pt1:
// 	.skip 4096

boot_pdtp511_high:
	.skip 4096
boot_pd510_high:
	.skip 4096
boot_pt0_high:
	.skip 4096

.align 16
tss:
	.rept CONFIG_MAX_CPUS
	.skip 0x68
	.endr
