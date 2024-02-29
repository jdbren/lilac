.text
.globl sys_call_handler
sys_call_handler:
    # already on stack: ss, sp, flags, cs, ip.
    pushl %ds
    pushl %ebp
    pushl %edi
    pushl %esi
    pushl %edx
    pushl %ecx
    pushl %ebx
    movw $0x10, %bp
    movw %bp, %ds
    leal syscall_table, %ebp
    movl (%ebp, %eax, 4), %eax
    calll *%eax
    popl %ebx
    popl %ecx
    popl %edx
    popl %esi
    popl %edi
    popl %ebp
    popl %ds
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
    .long __do_write
    .long open
    .long close
