.text
.globl sys_call_handler
sys_call_handler:
    # already on stack: ss, sp, flags, cs, ip.
    pushl %gs
    pushl %fs
    pushl %es
    pushl %ds
    pushl $0
    pushl %ebp
    pushl %edi
    pushl %esi
    pushl %edx
    pushl %ecx
    pushl %ebx
    leal syscall_table, %edx
    movl (%edx, %eax, 4), %eax
    calll *%eax
    popl %ebx
    popl %ecx
    popl %edx
    popl %esi
    popl %edi
    popl %ebp
    popl %eax
    popl %ds
    popl %es
    popl %fs
    popl %gs
    addl 4, %esp
    iret

restart:
    ret

exit:
    ret

read:
    ret

write:
    ret

open:
    ret

close:
    ret

.section .data
syscall_table:
    .long restart
    .long exit
    .long __do_fork
    .long read
    .long write
    .long open
    .long close
