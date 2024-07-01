#include <dirent.h>

int getdents(unsigned int fd, struct dirent *dirp, unsigned int count)
{
    int ret;
    asm volatile ("int $0x80"
                  : "=a" (ret)
                  : "a" (7), "b" (fd), "d" (dirp), "c" (count)
                  : "memory");
    return ret;
}
