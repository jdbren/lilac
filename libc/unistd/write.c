#include <unistd.h>

ssize_t write(int fd, const void *buf, size_t count)
{
    ssize_t ret;
    asm volatile ("int $0x80"
                  : "=a" (ret)
                  : "a" (4), "b" (fd), "c" (buf), "d" (count)
                  : "memory");
    return ret;
}
