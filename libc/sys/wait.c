#include <sys/wait.h>
#include <sys/syscall.h>

pid_t waitpid(pid_t pid, int *wstatus, int options)
{
    return syscall3(SYS_waitpid, pid, (long)wstatus, options);
}
