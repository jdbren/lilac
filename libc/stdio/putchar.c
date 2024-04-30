#include <stdio.h>
#include <unistd.h>

#if defined(__is_libk)
#include <lilac/tty.h>
#include <lilac/port.h>
#endif

int putchar(int ic)
{
	char c = (char)ic;
#if defined(__is_libk)
	WritePort(0xe9, c, 8);
	// graphics_putchar(c);
#else
	write(1, &c, sizeof(c));
#endif
	return ic;
}
