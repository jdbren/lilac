// uaccess.S - x86 assembly code for user access functions
#include <lilac/errno.h>

.text
/*
	esi - source address
	edi - destination address
	ecx - number of bytes to copy
*/
.global x86_user_copy_asm
x86_user_copy_asm:
	push %ebp
	mov %esp, %ebp
	pushf
	cld
1:	rep movsb
	mov %ecx, %eax
	popf
	pop %ebp
	ret
.section .exception, "a"
	.long 1b, .fault_handler
.text

/*
	esi - source address
	edi - destination address
	ecx - max number of bytes
*/
.global x86_strncpy_from_user_asm
x86_strncpy_from_user_asm:
	push %ebp
	mov %esp, %ebp
	pushf
2:	cmpb $0, (%esi)
	movsb
	loopnz 2b
	xor %eax, %eax
	popf
	pop %ebp
	ret
.section .exception, "a"
	.long 2b, .fault_handler
.text

.fault_handler:
	movl $-EFAULT, %eax
	popf
	pop %ebp
	ret
