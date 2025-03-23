#include <unistd.h>
#include <sys/syscall.h>

int execv(const char *pathname, char *const argv[])
{
    return syscall3(SYS_execve, (long)pathname, (long)argv, 0);
}
