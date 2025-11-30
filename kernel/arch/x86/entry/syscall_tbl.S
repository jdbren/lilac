// Syscall table and defs
#define __ASSEMBLY__
#include <lilac/errno.h>

sys_restart_syscall:
	mov $-ENOSYS, %eax
	ret

sys_rmdir:
	mov $-ENOSYS, %eax
	ret

#ifdef __x86_64__
.macro sc_tbl_entry name
	.quad sys_\name
.endm
#else
.macro sc_tbl_entry name
	.long sys_\name
.endm
#endif

.section .rodata
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
	sc_tbl_entry mkdir		# 19
	sc_tbl_entry rmdir		# 20
	sc_tbl_entry dup		# 21
	sc_tbl_entry pipe		# 22
	sc_tbl_entry brk		# 23
	sc_tbl_entry sbrk		# 24
	sc_tbl_entry reboot		# 25
	sc_tbl_entry dup2		# 26
	sc_tbl_entry kill		# 27
	sc_tbl_entry signal		# 28
	sc_tbl_entry getppid	# 29
	sc_tbl_entry sigaction  # 30
	sc_tbl_entry sigreturn  # 31
	sc_tbl_entry sigprocmask
	sc_tbl_entry sigpending # 33
	sc_tbl_entry getpgid    # 34
	sc_tbl_entry setpgid    # 35
	sc_tbl_entry getsid     # 36
	sc_tbl_entry setsid     # 37
	sc_tbl_entry sched_yield
	sc_tbl_entry fstat		# 39
	sc_tbl_entry ioctl      # 40
	sc_tbl_entry memfd_create
	sc_tbl_entry sigsuspend # 42
	sc_tbl_entry fcntl      # 43
	sc_tbl_entry mmap		# 44
	sc_tbl_entry munmap		# 45
	sc_tbl_entry nanosleep	# 46
	sc_tbl_entry gettimeofday
/*
	sc_tbl_entry link		# 26
	sc_tbl_entry unlink		# 27
	sc_tbl_entry symlink	# 28
	sc_tbl_entry readlink	# 29
	sc_tbl_entry chmod		# 30
	sc_tbl_entry chown		# 31
	sc_tbl_entry lstat		# 32
	sc_tbl_entry fstat		# 33
	sc_tbl_entry fcntl		# 34
	sc_tbl_entry ioctl		# 35
	sc_tbl_entry getuid		# 36
	sc_tbl_entry setuid		# 37
	sc_tbl_entry alarm		# 38
	sc_tbl_entry pause		# 39
	sc_tbl_entry utime		# 40
	sc_tbl_entry mknod		# 41
	sc_tbl_entry access		# 42
	sc_tbl_entry sync 		# 43
	sc_tbl_entry kill		# 44
	sc_tbl_entry rename		# 45
	sc_tbl_entry signal		# 46
	sc_tbl_entry dup2		# 47
*/
syscall_table_end:
