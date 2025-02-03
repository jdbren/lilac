#include <asm/segments.h>

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
.align 16
tss:
	.rept 8
	.skip 0x68
	.endr
