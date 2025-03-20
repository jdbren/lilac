.text
.globl _start
#ifdef ARCH_x86_64
_start:
	mov 	8(%rsp), %rsi # argv
	mov 	(%rsp), %rdi  # argc

	xor 	%rbp, %rbp    # mark the end of stack frames
	xor 	%rax, %rax

	call 	main          # call main

	mov 	%rax, %rdi    # first argument of _exit
	xor  	%rax, %rax
	call 	exit          # call _exit
#else
_start:
	mov 	%esp, %ebp    # set up the stack frame pointer
	push 	4(%ebp)       # argv
	push 	(%ebp)        # argc

	xor 	%ebp, %ebp    # mark the end of stack frames
	xor 	%eax, %eax

	call 	main          # call main
	addl 	$8, %esp      # clean up the stack

	push 	%eax          # first argument of _exit
	xor  	%eax, %eax
	call 	exit          # call _exit
#endif
