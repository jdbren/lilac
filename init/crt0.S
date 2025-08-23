.text
.globl _start
#ifdef __x86_64__
_start:
	xor 	%rbp, %rbp    # mark the end of stack frames

	mov     16(%rsp), %rax
	mov     %rax, (environ) # envp

	call    __libc_init_array

	xor 	%rax, %rax
	mov     16(%rsp), %rdx
	mov 	8(%rsp), %rsi # argv
	mov 	(%rsp), %rdi  # argc
	call 	main          # call main

	mov 	%rax, %rdi    # first argument of _exit
	xor  	%rax, %rax
	call 	exit          # call _exit
#else
_start:
	xor 	%ebp, %ebp    # mark the end of stack frames

	mov     8(%esp), %eax
	mov     %eax, (environ)

	call    __libc_init_array

	xor 	%eax, %eax
	# argc, argv already on the stack
	call 	main          # call main

	push 	%eax          # first argument of _exit
	xor  	%eax, %eax
	call 	exit          # call _exit
#endif
