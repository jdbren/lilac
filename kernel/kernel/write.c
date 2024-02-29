#include <sys/types.h>
#include <kernel/tty.h>

ssize_t __do_write(void)
{
    register int fd asm("ebx");
    register const void *buf asm("edx");
    register size_t count asm("ecx");

    terminal_write(buf, count);

    return count;
}
