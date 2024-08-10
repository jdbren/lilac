#include <unistd.h>

void _exit(int status)
{
    asm volatile ("int $0x80" : : "a" (1), "b" (status));
}
