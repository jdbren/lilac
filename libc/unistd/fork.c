#include <unistd.h>
#include <sys/syscall.h>

pid_t fork(void)
{
    return syscall0(SYS_fork);
}
