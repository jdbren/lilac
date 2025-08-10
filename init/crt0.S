.text
.globl _start
#ifdef __x86_64__
_start:
	xor 	%rbp, %rbp    # mark the end of stack frames
	xor 	%rax, %rax

	call    __libc_init_array

	mov 	8(%rsp), %rsi # argv
	mov 	(%rsp), %rdi  # argc
	call 	main          # call main

	mov 	%rax, %rdi    # first argument of _exit
	xor  	%rax, %rax
	call 	exit          # call _exit
#else
_start:
	xor 	%ebp, %ebp    # mark the end of stack frames
	xor 	%eax, %eax

	call    __libc_init_array
	# argc, argv already on the stack
	call 	main          # call main

	push 	%eax          # first argument of _exit
	xor  	%eax, %eax
	call 	exit          # call _exit
#endif
