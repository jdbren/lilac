.text
.extern do_syscall
.globl sys_call_handler
sys_call_handler:
    # already on stack: ss, sp, flags, cs, ip.
    pushl %gs
    pushl %fs
    pushl %es
    pushl %ds
    pushl -ENOSYS
    pushl %ebp
    pushl %edi
    pushl %esi
    pushl %edx
    pushl %ecx
    pushl %ebx
    pushl %eax
    call do_syscall
    addl 4, %esp
    popl %ebx
    popl %ecx
    popl %edx
    popl %esi
    popl %edi
    popl %ebp
    popl -ENOSYS
    popl %ds
    popl %es
    popl %fs
    popl %gs
    addl 4, %esp
    iretd