#include <sys/wait.h>

pid_t waitpid(pid_t pid, int *wstatus, int options)
{
    asm volatile("int $0x80" : : "a" (7), "b" (pid), "c" (wstatus), "d" (options));
}
