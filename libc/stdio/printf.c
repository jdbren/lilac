#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

char *convert(unsigned int num, int base);



int printf(const char* restrict format, ...)
{
	int written = 0;
	va_list parameters;
	va_start(parameters, format);
	written = vprintf(format, parameters);
	va_end(parameters);
	return written;
}
