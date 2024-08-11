#include <stdio.h>
#include <stdint.h>
#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

static bool print(const char *data, size_t length)
{
	const unsigned char* bytes = (const unsigned char*) data;
	for (size_t i = 0; i < length; i++)
		if (putchar(bytes[i]) == EOF)
			return false;
	return true;
}

char *convert(unsigned long num, int base)
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

int vprintf(const char *restrict format, va_list args)
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

		const char *format_begun_at = format++;
		bool left_justify = false;
		bool zero_pad = false;
		bool use_width = false;
		unsigned int width = UINT32_MAX;
		if (*format == '-') {
			format++;
			left_justify = true;
		}
		if (*format == '+') {
			format++;
		}
		if (*format == ' ') {
			format++;
		}
		if (*format == '#') {
			format++;
		}
		if (*format == '0') {
			format++;
			zero_pad = true;
		}
		if (isdigit(*format)) {
			use_width = true;
			width = format[0] - '0';
			format++;
		}
		if (*format == '.') {
			format++;
			if (isdigit(*format)) {
				use_width = true;
				zero_pad = true;
				width = format[0] - '0';
				format++;
			}
		}

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
			unsigned len = strlen(str);
			if (use_width) {
				if (width < len)
					len = width;
				while (len < width && !left_justify) {
					if (!print(" ", 1))
						return -1;
					written++;
					width--;
				}
			}
			if (maxrem < len) {
				// TODO: Set errno to EOVERFLOW.
				return -1;
			}
			if (!print(str, len))
				return -1;
			written += len;
			while (use_width && len < width && left_justify) {
				if (!print(" ", 1))
					return -1;
				written++;
				width--;
			}
		}
		else if (*format == 'd') {
			format++;
			int i = va_arg(args, int);
			if (i < 0) {
				i = -i;
				putchar('-');
			}
			char *s = convert(i, 10);
			unsigned len = strlen(s);
			if (use_width) {
				if (width < len)
					len = width;
				while (len < width && !left_justify) {
					if (zero_pad) {
						if (!print("0", 1))
							return -1;
					}
					else {
						if(!print(" ", 1))
							return -1;
					}
					written++;
					width--;
				}
			}
			if (!print(s, len))
				return -1;
			written += len;
		}
		else if (*format == 'x' || *format == 'p' || *format == 'X') {
			format++;
			unsigned int i = va_arg(args, unsigned int);
			char *s = convert(i, 16);
			unsigned int len = strlen(s);
			if (use_width) {
				if (width < len)
					len = width;
				while (len < width && !left_justify) {
					if (zero_pad) {
						if (!print("0", 1))
							return -1;
					}
					else {
						if(!print(" ", 1))
							return -1;
					}
					written++;
					width--;
				}
			}
			if (!print(s, len))
				return -1;
			written += len;
		}
		else if (*format == 'u') {
			format++;
			unsigned int i = va_arg(args, unsigned int);
			char *s = convert(i, 10);
			unsigned len = strlen(s);
			len = (width < len) ? width : len;
			if (!print(s, len))
				return -1;
			written += len;
		}
		else if (*format == 'f') {
			format++;
			double f = va_arg(args, double);
			int i = (int)f;
			char *s = convert(i, 10);
			unsigned len = strlen(s);
			if (!print(s, len))
				return -1;
			written += len;
			putchar('.');
			f -= i;
			f *= 1000000;
			i = (int)f;
			s = convert(i, 10);
			len = strlen(s);
			if (!print(s, len))
				return -1;
			written += len;
		}
		else if (*format == 'l') {
			format++;
			if (*format == 'l') {
				format++;
				if (*format == 'd') {
					format++;
					long long i = va_arg(args, long long);
					if (i < 0) {
						i = -i;
						putchar('-');
					}
					char *s = convert(i, 10);
					unsigned len = strlen(s);
					if (!print(s, len))
						return -1;
					written += len;
				}
				else if (*format == 'x') {
					format++;
					long long i = va_arg(args, long long);
					char *s = convert(i, 16);
					unsigned len = strlen(s);
					if (!print(s, len))
						return -1;
					written += len;
				}
				else if (*format == 'u') {
					format++;
					unsigned long long i = va_arg(args, unsigned long long);
					char *s = convert(i, 10);
					unsigned len = strlen(s);
					if (!print(s, len))
						return -1;
					written += len;
				}
			}
			else if (*format == 'd') {
				format++;
				long i = va_arg(args, long);
				if (i<0) {
					i = -i;
					putchar('-');
				}
				char *s = convert(i, 10);
				unsigned len = strlen(s);
				if (!print(s, len))
					return -1;
				written += len;
			}
			else if (*format == 'x') {
				format++;
				long i = va_arg(args, long);
				char *s = convert(i, 16);
				unsigned len = strlen(s);
				if (!print(s, len))
					return -1;
				written += len;
			}
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