#include <limits.h>
#include <lilac/libc.h>

int printf(const char *restrict format, ...)
{
	int written;
	va_list parameters;

	va_start(parameters, format);
	written = vprintf(format, parameters);
	va_end(parameters);

	return written;
}
