#include <unistd.h>
#include <sys/syscall.h>

void __attribute__((noreturn)) _exit(int status)
{
    while (1)
        syscall1(SYS_exit, status);
    __builtin_unreachable();
}
