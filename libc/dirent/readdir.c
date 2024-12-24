#include <dirent.h>

int getdents(unsigned int fd, struct dirent *dirp, unsigned int count)
{
    int ret;
    asm volatile ("int $0x80"
                  : "=a" (ret)
                  : "a" (12), "b" (fd), "c" (dirp), "d" (count)
                  : "memory");
    return ret;
}
