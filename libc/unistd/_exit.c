#include <unistd.h>
#include <sys/syscall.h>

void __attribute__((noreturn)) _exit(int status)
{
    while (1)
        asm ("int $0x80" :: "a"(SYS_exit), "b"(status));
    __builtin_unreachable();
}
