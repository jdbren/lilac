.text
.globl _start
_start: # _start is the entry point known to the linker
    xor %ebp, %ebp    # effectively RBP := 0, mark the end of stack frames
    xor %eax, %eax    # per ABI and compatibility with icc

    call main         # call main

    mov %eax, %edi    # transfer the return of main to the first argument of _exit
    xor %eax, %eax    # per ABI and compatibility with icc
    call _exit        # call _exit

1:  nop
    jmp 1b

_exit:
    movl $1, %eax     # syscall number for exit
    movl %edi, %ebx   # exit code
2:  int $0x80         # invoke syscall
    jmp 2b            # loop forever
