#include <unistd.h>

pid_t fork(void)
{
    pid_t pid;
    asm volatile ("int $0x80" : "=a" (pid) : "a" (2));
    return pid;
}
