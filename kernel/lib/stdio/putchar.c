#include <lilac/console.h>
#include <lilac/port.h>

int putchar(int ic)
{
	char c = (char)ic;
	write_port(0xe9, c, 8);
	if (write_to_screen)
		console_write(NULL, &c, 1);
	return ic;
}
