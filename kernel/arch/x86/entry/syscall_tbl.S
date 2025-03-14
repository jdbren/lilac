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

#ifdef ARCH_x86_64
.macro sc_tbl_entry name
	.quad sys_\name
.endm
#else
.macro sc_tbl_entry name
	.long sys_\name
.endm
#endif

.data
.globl syscall_table
.globl syscall_table_end
syscall_table:
	sc_tbl_entry restart_syscall
	sc_tbl_entry exit		# 1
	sc_tbl_entry fork		# 2
	sc_tbl_entry read		# 3
	sc_tbl_entry write		# 4
	sc_tbl_entry open		# 5
	sc_tbl_entry close		# 6
	sc_tbl_entry waitpid	# 7
	sc_tbl_entry create		# 8
	sc_tbl_entry execve		# 9
	sc_tbl_entry chdir		# 10
	sc_tbl_entry time		# 11
	sc_tbl_entry stat		# 12
	sc_tbl_entry lseek		# 13
	sc_tbl_entry getpid		# 14
	sc_tbl_entry mount		# 15
	sc_tbl_entry umount		# 16
	sc_tbl_entry getdents	# 17
	sc_tbl_entry getcwd		# 18
	// sc_tbl_entry mkdir	# 19
	// sc_tbl_entry rmdir	# 20
	// sc_tbl_entry dup		# 21
	// sc_tbl_entry pipe	# 22
syscall_table_end:
