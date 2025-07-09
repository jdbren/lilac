#include <lilac/libc.h>

static char * character(char *str, char c, char *end)
{
    if (str < end) {
        *str++ = c;
    }
    return str;
}

static char * string(char *str, const char *s, char *end)
{
    if (!s) {
        s = "(null)";
    }
    while (*s && str < end) {
        *str++ = *s++;
    }
    return str;
}

static char * convert(unsigned long num, int base)
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


int vsnprintf(char *restrict buf, size_t size, const char *restrict fmt,
    va_list args)
{
    char *str, *end;

    if (size > INT_MAX)
        return 0;

    str = buf;
    end = buf + size;

    if (end <= str)
        return 0;

    while (*fmt) {
        if (str == end) {
            *str = '\0';
            break;
        }
        if (*fmt != '%') {
            *str++ = *fmt++;
            continue;
        } else {
            fmt++;
        }

        switch (*fmt) {
            case 'c': {
                fmt++;
                str = character(str, (unsigned char) va_arg(args, int), end);
                continue;
            }
            case 's': {
                fmt++;
                str = string(str, va_arg(args, const char *), end);
                continue;
            }
            // incomplete
        }
    }
    *str = '\0';
    return str - buf;
}

int snprintf(char *restrict buf, size_t size, const char *restrict fmt, ...)
{
    va_list args;
    int written;

    va_start(args, fmt);
    written = vsnprintf(buf, size, fmt, args);
    va_end(args);

    return written;
}
