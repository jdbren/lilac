#include <unistd.h>

ssize_t read(int fd, void *buf, size_t count)
{
    ssize_t ret;
    asm volatile ("int $0x80"
                  : "=a" (ret)
                  : "a" (3), "b" (fd), "c" (buf), "d" (count)
                  : "memory");
    return ret;
}
