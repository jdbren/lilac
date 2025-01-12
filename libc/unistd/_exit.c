#include <unistd.h>
#include <sys/syscall.h>

void __attribute__((noreturn)) _exit(int status)
{
    asm volatile ("int $0x80" : : "a"(SYS_exit), "b"(status));
    __builtin_unreachable();
}
