#include <asm/segments.h>

.global gdt_init
.type gdt_init, @function
gdt_init:
	leal gdt_descr, %eax
	movl $gdt_entries, 2(%eax)
	lgdt (%eax)
	call tss_init
	ret

tss_init:
	leal tss, %eax
	movl $0, 4(%eax)
	movl $__KERNEL_DS, 8(%eax)

	movl (gdt_descr+2), %edx
	addl $5 * 8, %edx

	addl $2, %edx
	movw %ax, (%edx)

	addl $2, %edx
	shrl $16, %eax
	movb %al, (%edx)

	addl $3, %edx
	shrl $8, %eax
	movb %al, (%edx)

	call flush_tss
	ret

.global set_tss_esp0
set_tss_esp0:
	leal tss, %eax
	movl %esp, 4(%eax)
	ret

flush_tss:
	movw $(5 * 8) | 0, %ax  # RPL 0
	ltr %ax
	ret


.data
.align 4
.word 0
gdt_descr:
	.word 6 * 64 - 1
	.long gdt_entries

.align 8
gdt_entries:
	.quad 0x0
	.quad 0x00CF9A000000FFFF
	.quad 0x00CF92000000FFFF
	.quad 0x00CFFA000000FFFF
	.quad 0x00CFF2000000FFFF
	.quad 0x0000890000000067

.bss
.align 4
tss:
	.skip 0x68
