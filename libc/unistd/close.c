#include <unistd.h>

int close(int fd)
{
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "0" (6), "b" (fd));
    return ret;
}
