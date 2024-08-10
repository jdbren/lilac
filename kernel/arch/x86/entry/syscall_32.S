.text
.globl sys_call_handler
sys_call_handler:
    cmpl $0x8, %eax
    jg .Lerror
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
    xorl %ebp, %ebp
    calll *%eax
    popl %ebx
    popl %ecx
    popl %edx
    popl %esi
    popl %edi
    popl %ebp
    popl %ds
.Lerror:
    iret

sys_restart:
    ret

sys_exit:
    ret

read:
    ret

close:
    ret

getdents:
    ret

.data
syscall_table:
    .long sys_restart
    .long sys_exit
    .long sys_fork
    .long sys_read
    .long sys_write
    .long sys_open
    .long sys_close
    .long sys_getdents
