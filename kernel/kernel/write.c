#include <sys/types.h>
#include <lilac/tty.h>

ssize_t __do_write(void)
{
    register int fd asm("ebx");
    register const char *buf asm("edx");
    register size_t count asm("ecx");

    graphics_putchar(*buf);

    return count;
}
