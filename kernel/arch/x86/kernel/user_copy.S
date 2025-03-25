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
	cld
1:	rep movsb
	mov %ecx, %eax
	pop %rbp
	retq
.section .exception, "a"
	.quad 1b, .fault_handler
.text
#else
x86_user_copy_asm:
	push %ebp
	mov %esp, %ebp
	cld
1:	rep movsb
	mov %ecx, %eax
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
	returns: number of bytes remaining in cx
*/
.global x86_strncpy_from_user_asm
#ifdef ARCH_x86_64
x86_strncpy_from_user_asm:
	push %rbp
	mov  %rsp, %rbp
	cld
2:	cmpb $0, (%rsi)
	movsb
	loopnz 2b
	mov %ecx, %eax
	pop %rbp
	ret
.section .exception, "a"
	.quad 2b, .fault_handler
.text
#else
x86_strncpy_from_user_asm:
	push %ebp
	mov %esp, %ebp
	cld
2:	cmpb $0, (%esi)
	movsb
	loopnz 2b
	mov %ecx, %eax
	pop %ebp
	ret
.section .exception, "a"
	.long 2b, .fault_handler
.text
#endif

/*
	di - string ptr
	si - max length
*/
.global x86_strnlen_user_asm
#ifdef ARCH_x86_64
x86_strnlen_user_asm:
    push    %rbp
    mov     %rsp, %rbp
    mov     %esi, %ecx        # ECX = max length
1:  cmpb    $0, (%rdi)
    inc     %rdi
    loopnz  1b
    cmpl    $0, %ecx
    je      2f                # if ECX==0, no null was found; length = max
    inc     %ecx              # otherwise, adjust ECX since we advanced one extra byte
2:  mov     %esi, %eax
    sub     %ecx, %eax        # computed length = max - adjusted remaining count
    pop     %rbp
    retq
.section .exception, "a"
	.quad 1b, .fault_handler
.text
#else
x86_strnlen_user_asm:
    push    %ebp
    mov     %esp, %ebp
    mov     %esi, %ecx        # ECX = max length
1:  cmpb    $0, (%edi)
    inc     %edi
    loopnz  1b
    cmpl    $0, %ecx
    je      2f                # if ECX==0, no null was found; length = max
    inc     %ecx              # otherwise, adjust ECX since we advanced one extra byte
2:  mov     %esi, %eax
    sub     %ecx, %eax        # computed length = max - adjusted remaining count
    pop     %ebp
    ret
.section .exception, "a"
	.long 1b, .fault_handler
.text
#endif

.fault_handler:
	mov $-EFAULT, %eax
#ifdef ARCH_x86_64
	pop %rbp
	retq
#else
	pop %ebp
	ret
#endif
