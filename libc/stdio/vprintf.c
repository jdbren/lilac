#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

static bool print(const char* data, size_t length)
{
	const unsigned char* bytes = (const unsigned char*) data;
	for (size_t i = 0; i < length; i++)
		if (putchar(bytes[i]) == EOF)
			return false;
	return true;
}

char *convert(unsigned int num, int base)
{
    static char Representation[]= "0123456789ABCDEF";
    static char buffer[50];
    char *ptr;

    ptr = &buffer[49];
    *ptr = '\0';

    do {
        *--ptr = Representation[num%base];
        num /= base;
    } while (num != 0);

    return(ptr);
}

int vprintf(register const char *restrict format, va_list args)
{
	int written = 0;

	while (*format != '\0') {
		size_t maxrem = INT_MAX - written;

		if (format[0] != '%' || format[1] == '%') {
			if (format[0] == '%')
				format++;
			size_t amount = 1;
			while (format[amount] && format[amount] != '%')
				amount++;
			if (maxrem < amount) {
				// TODO: Set errno to EOVERFLOW.
				return -1;
			}
			if (!print(format, amount))
				return -1;
			format += amount;
			written += amount;
			continue;
		}

		const char* format_begun_at = format++;

		if (*format == 'c') {
			format++;
			char c = (char) va_arg(args, int /* char promotes to int */);
			if (!maxrem) {
				// TODO: Set errno to EOVERFLOW.
				return -1;
			}
			if (!print(&c, sizeof(c)))
				return -1;
			written++;
		}
		else if (*format == 's') {
			format++;
			const char* str = va_arg(args, const char*);
			size_t len = strlen(str);
			if (maxrem < len) {
				// TODO: Set errno to EOVERFLOW.
				return -1;
			}
			if (!print(str, len))
				return -1;
			written += len;
		}
		else if (*format == 'd') {
			format++;
			int i = va_arg(args, int);
			if (i<0) {
				i = -i;
				putchar('-');
			}
			char *s = convert(i, 10);
			int len = strlen(s);
			if (!print(s, len))
				return -1;
			written += len;
		}
		else if (*format == 'x') {
			format++;
			int i = va_arg(args, int);
			char *s = convert(i, 16);
			*--s = 'x';
			*--s = '0';
			int len = strlen(s);
			if (!print(s, len))
				return -1;
			written += len;
		}
		else {
			format = format_begun_at;
			size_t len = strlen(format);
			if (maxrem < len) {
				// TODO: Set errno to EOVERFLOW.
				return -1;
			}
			if (!print(format, len))
				return -1;
			written += len;
			format += len;
		}
	}
    return written;
}