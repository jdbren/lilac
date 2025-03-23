#include <fcntl.h>
#include <sys/syscall.h>

int open(const char *pathname, int flags)
{
    return syscall3(SYS_open, (long)pathname, flags, 0);
}
