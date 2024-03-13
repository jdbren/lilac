#include <stdio.h>
#include <unistd.h>

#if defined(__is_libk)
#include <kernel/tty.h>
#include <kernel/io.h>
#endif

int putchar(int ic)
{
	char c = (char)ic;
#if defined(__is_libk)
	outb(0xe9, c);
	graphics_putchar(c);
#else
	write(1, &c, sizeof(c));
#endif
	return ic;
}
