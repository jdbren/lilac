#include <fcntl.h>
#include <sys/syscall.h>

int open(const char *pathname, int flags)
{
    return syscall2(SYS_open, (long)pathname, flags);
}
