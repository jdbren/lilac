#include <unistd.h>

int execv(const char *pathname, char *const argv[])
{
    int ret;
    asm volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (11), "b" (pathname), "c" (argv), "d" (NULL)
    );
    return ret;
}
