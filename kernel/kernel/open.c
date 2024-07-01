#include <lilac/fs.h>

int __do_open()
{
    register void *path asm("ebx");
    register int flags asm("edx");

    return open(path, flags, 0);
}

int __do_getdents()
{
    register int fd asm("ebx");
    register void *buf asm("edx");
    register int count asm("ecx");

    return getdents(fd, buf, count);
}
