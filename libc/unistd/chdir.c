#include <unistd.h>
#include <sys/syscall.h>

int chdir(const char *path)
{
    return syscall1(SYS_chdir, (long)path);
}
