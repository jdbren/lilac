#ifndef _LIBC_SYSCALL_H
#define _LIBC_SYSCALL_H

#define SYS_restart_syscall 0
#define SYS_exit 1
#define SYS_fork 2
#define SYS_read 3
#define SYS_write 4
#define SYS_open 5
#define SYS_close 6
#define SYS_waitpid 7
#define SYS_creat 8
#define SYS_execve 9
#define SYS_chdir 10
#define SYS_time 11
#define SYS_stat 12
#define SYS_lseek 13
#define SYS_getpid 14
#define SYS_mount 15
#define SYS_umount 16
#define SYS_getdents 17
#define SYS_getcwd 18
#define SYS_mkdir 19
#define SYS_rmdir 20
#define SYS_dup 21
#define SYS_pipe 22

#ifdef ARCH_x86_64
static inline long syscall0(long number)
{
    long ret;
    asm volatile ("syscall" : "=a"(ret) : "a"(number) : "rcx", "r11", "memory", "rdi");
    return ret;
}

static inline long syscall1(long number, long arg0)
{
    long ret;
    asm volatile (
        "syscall" : "=a"(ret) : "a"(number), "D"(arg0)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall2(long number, long arg0, long arg1)
{
    long ret;
    asm volatile (
        "syscall" : "=a"(ret) : "a"(number), "D"(arg0), "S"(arg1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall3(long number, long arg0, long arg1, long arg2)
{
    long ret;
    asm volatile (
        "syscall" : "=a"(ret) : "a"(number), "D"(arg0), "S"(arg1), "d"(arg2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long syscall4(long number, long arg0, long arg1, long arg2,
    long arg3)
{
    long ret;
    asm volatile (
        "mov %4, %r10\n\t"
        "syscall" : "=a"(ret) :
        "a"(number), "D"(arg0), "S"(arg1), "d"(arg2), "r"(arg3)
        : "rcx", "r11", "memory", "r10"
    );
    return ret;
}

static inline long syscall5(long number, long arg0, long arg1, long arg2,
    long arg3, long arg4)
{
    long ret;
    asm volatile (
        "mov %4, %r10\n\t"
        "mov %5, %r8\n\t"
        "syscall" : "=a"(ret) :
        "a"(number), "D"(arg0), "S"(arg1), "d"(arg2), "r"(arg3), "r"(arg4)
        : "rcx", "r11", "memory", "r10", "r8"
    );
    return ret;
}
#else /* 32-bit: */
static inline long syscall0(long number)
{
    long ret;
    asm volatile(
        "int $0x80" :
        "=a"(ret) :
        "a"(number)
    );
    return ret;
}

static inline long syscall1(long number, long arg0)
{
    long ret;
    asm volatile(
        "int $0x80" :
        "=a"(ret) :
        "a"(number), "b"(arg0)
    );
    return ret;
}

static inline long syscall2(long number, long arg0, long arg1)
{
    long ret;
    asm volatile(
        "int $0x80" :
        "=a"(ret) :
        "a"(number), "b"(arg0), "c"(arg1)
    );
    return ret;
}

static inline long syscall3(long number, long arg0, long arg1, long arg2)
{
    long ret;
    asm volatile(
        "int $0x80" :
        "=a"(ret) :
        "a"(number), "b"(arg0), "c"(arg1), "d"(arg2)
    );
    return ret;
}

static inline long syscall4(long number, long arg0, long arg1, long arg2, long arg3)
{
    long ret;
    asm volatile(
        "int $0x80" :
        "=a"(ret) :
        "a"(number), "b"(arg0), "c"(arg1), "d"(arg2), "S"(arg3)
    );
    return ret;
}

static inline long syscall5(long number, long arg0, long arg1, long arg2, long arg3, long arg4)
{
    long ret;
    asm volatile(
        "int $0x80" :
        "=a"(ret) :
        "a"(number), "b"(arg0), "c"(arg1), "d"(arg2), "S"(arg3), "D"(arg4)
    );
    return ret;
}
#endif // ARCH_x86_64

#endif // _LIBC_SYSCALL_H
