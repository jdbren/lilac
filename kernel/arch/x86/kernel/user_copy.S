// uaccess.S - x86 assembly code for user access functions
#define __ASSEMBLY__
#include <lilac/errno.h>

.text
/*
	si - source address
	di - destination address
	cx - number of bytes to copy
*/
.global x86_user_copy_asm
#ifdef ARCH_x86_64
x86_user_copy_asm:
	push %rbp
	mov  %rsp, %rbp
	pushf
	cld
1:	rep movsb
	mov %ecx, %eax
	popf
	pop %rbp
	retq
.section .exception, "a"
	.quad 1b, .fault_handler
.text
#else
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
#endif


/*
	si - source address
	di - destination address
	cx - max number of bytes
*/
.global x86_strncpy_from_user_asm
#ifdef ARCH_x86_64
x86_strncpy_from_user_asm:
	push %rbp
	mov  %rsp, %rbp
	pushf
2:	cmpb $0, (%rsi)
	movsb
	loopnz 2b
	mov %ecx, %eax
	popf
	pop %rbp
	ret
.section .exception, "a"
	.quad 2b, .fault_handler
.text
#else
x86_strncpy_from_user_asm:
	push %ebp
	mov %esp, %ebp
	pushf
2:	cmpb $0, (%esi)
	movsb
	loopnz 2b
	mov %ecx, %eax
	popf
	pop %ebp
	ret
.section .exception, "a"
	.long 2b, .fault_handler
.text
#endif


.fault_handler:
	mov $-EFAULT, %eax
	popf
#ifdef ARCH_x86_64
	pop %rbp
	retq
#else
	pop %ebp
	ret
#endif
