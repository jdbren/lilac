.text
.globl syscall_handler
syscall_handler:
	# already on stack: ss, sp, flags, cs, ip.
	pushl %gs
	pushl %fs
	pushl %es
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
	popl %es
	popl %fs
	popl %gs
.Lerror:
	iret

sys_restart:
	ret

sys_fork:
	movl %esp, %eax
	addl $4, %eax
	pushl %eax
	# ia32_do_fork(&regs)
	call ia32_do_fork
	addl $4, %esp
	ret

/*
int arch_return_from_fork(void *regs);
	Params:
	4(%esp) = ptr to regs

	Jump to usermode in new process.
*/
.globl arch_return_from_fork
arch_return_from_fork:
	movl 4(%esp), %eax

	# restore registers
	movl (%eax), %ebx
	movl 4(%eax), %ecx
	movl 8(%eax), %edx
	movl 12(%eax), %esi
	movl 16(%eax), %edi
	movl 20(%eax), %ebp
	movw 24(%eax), %ds
	movw 28(%eax), %es
	movw 32(%eax), %fs
	movw 36(%eax), %gs

	# restore stack
	pushl 56(%eax) # ss
	pushl 52(%eax) # esp
	pushl 48(%eax) # eflags
	pushl 44(%eax) # cs
	pushl 40(%eax) # eip

	# return to usermode
	movl $0, %eax
	iret

sys_link:
	ret
sys_unlink:
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
	.long sys_waitpid
	.long sys_create
	.long sys_link
	.long sys_unlink
	.long sys_execve
	# ...
	.long sys_getdents # 12
	.long sys_getcwd
	.long sys_mkdir
	.long sys_mount
syscall_table_end:
