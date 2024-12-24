.text
.globl _start
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
