#include <unistd.h>

pid_t fork(void)
{
    asm volatile ("int $0x80" : : "a" (2));
    return 0;
}
