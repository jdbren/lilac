#include <unistd.h>
#include <errno.h>
#include <sys/syscall.h>

char* getcwd(char buf[], size_t size)
{
    long ret = syscall2(SYS_getcwd, (long)buf, size);
    if (ret < 0) {
        errno = -ret;
        return NULL;
    }
    return (char*)buf;
}
