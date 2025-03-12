// Syscall table and defs

sys_restart:
	ret

sys_restart_syscall:
	ret

sys_chdir:
	ret

sys_time:
	ret

sys_stat:
	ret

sys_getpid:
	ret

sys_umount:
	ret

.data
.globl syscall_table
.globl syscall_table_end
syscall_table:
	.long sys_restart_syscall
	.long sys_exit		# 1
	.long sys_fork 		# 2
	.long sys_read		# 3
	.long sys_write		# 4
	.long sys_open 		# 5
	.long sys_close 	# 6
	.long sys_waitpid 	# 7
	.long sys_create 	# 8
	.long sys_execve 	# 9
	.long sys_chdir 	# 10
	.long sys_time 		# 11
	.long sys_stat 		# 12
	.long sys_lseek 	# 13
	.long sys_getpid 	# 14
	.long sys_mount 	# 15
	.long sys_umount 	# 16
	.long sys_getdents 	# 17
	.long sys_getcwd 	# 18
	// .long sys_mkdir 	# 19
	// .long sys_rmdir 	# 20
	// .long sys_dup 	# 21
	// .long sys_pipe 	# 22
syscall_table_end:
