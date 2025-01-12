#include <dirent.h>
#include <sys/syscall.h>

int getdents(unsigned int fd, struct dirent *dirp, unsigned int count)
{
    return syscall3(SYS_getdents, fd, (long)dirp, count);
}
