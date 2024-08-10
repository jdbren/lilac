#include <fcntl.h>

int open(const char *pathname, int flags)
{
    int ret;
    asm volatile ("int $0x80"
                  : "=a" (ret)
                  : "a" (5), "b" (pathname), "c" (flags)
                  : "memory");
    return ret;
}
