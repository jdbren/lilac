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
#ifdef __x86_64__
x86_user_copy_asm:
	cld
	stac
1:	rep movsb
	clac
	mov %ecx, %eax
	retq
.section .exception, "a"
	.quad 1b, .fault_handler
.text
#else
x86_user_copy_asm:
	cld
1:	rep movsb
	mov %ecx, %eax
	ret
.section .exception, "a"
	.long 1b, .fault_handler
.text
#endif


/*
	si - source address
	di - destination address
	cx - max number of bytes
	returns: number of bytes copied
*/
.global x86_strncpy_from_user_asm
#ifdef __x86_64__
x86_strncpy_from_user_asm:
	cld
	stac
	mov %rcx, %r8
1:	cmpb $0, (%rsi)
	movsb
	loopnz 1b
	clac
	mov %r8d, %eax
	sub %ecx, %eax
	ret
.section .exception, "a"
	.quad 1b, .fault_handler
.text
#else
x86_strncpy_from_user_asm:
	cld
	mov %ecx, %edx
1:	cmpb $0, (%esi)
	movsb
	loopnz 1b
	mov %edx, %eax
	sub %ecx, %eax
	ret
.section .exception, "a"
	.long 1b, .fault_handler
.text
#endif

/*
	di - string ptr
	si - max length
*/
.global x86_strnlen_user_asm
#ifdef __x86_64__
x86_strnlen_user_asm:
	mov     %esi, %ecx        # ECX = max length
	mov     %esi, %eax
	test    %ecx, %ecx
	jz      2f
	stac
1:	cmpb    $0, (%rdi)
	lea     1(%rdi), %rdi
	loopnz  1b
	clac
	jnz     2f                # no NUL in max bytes: length = max
	sub     %ecx, %eax        # max - (bytes left after the NUL)
	dec     %eax              # don't count the NUL
2:	retq
.section .exception, "a"
	.quad 1b, .fault_handler
.text
#else
x86_strnlen_user_asm:
	mov     %esi, %ecx        # ECX = max length
	mov     %esi, %eax
	test    %ecx, %ecx
	jz      2f
1:	cmpb    $0, (%edi)
	lea     1(%edi), %edi
	loopnz  1b
	jnz     2f                # no NUL in max bytes: length = max
	sub     %ecx, %eax        # max - (bytes left after the NUL)
	dec     %eax              # don't count the NUL
2:	ret
.section .exception, "a"
	.long 1b, .fault_handler
.text
#endif

/*
	rdi - pointer to user memory
	rsi - expected value
	rdx - desired value to write
	rcx - pointer to store actual value (or NULL)
	returns 1 if value matched or 0 if not
*/
#ifdef __x86_64__
.global arch_cmpxchg4_user
arch_cmpxchg4_user:
	mov %esi, %eax
	stac
1:	lock cmpxchgl %edx, (%rdi)
	clac
	mov %eax, %r8d
	sete %al
	movzbq %al, %rax
	test %rcx, %rcx
	jz 2f
	movl %r8d, (%rcx)
2:
	retq
.section .exception, "a"
	.quad 1b, .fault_handler
.text

.global arch_cmpxchg8_user
arch_cmpxchg8_user:
	mov %rsi, %rax
	stac
1:	lock cmpxchgq %rdx, (%rdi)
	clac
	mov %rax, %r8
	sete %al
	movzbq %al, %rax
	test %rcx, %rcx
	jz 2f
	movq %r8, (%rcx)
2:
	retq
.section .exception, "a"
	.quad 1b, .fault_handler
.text
#endif

.fault_handler:
	mov $-EFAULT, %eax
#ifdef __x86_64__
	clac
	cltq
	retq
#else
	ret
#endif
