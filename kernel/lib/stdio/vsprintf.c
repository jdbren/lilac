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
        *--ptr = Representation[num % base];
        num /= base;
    } while (num != 0);

    return(ptr);
}

static char * convert_signed(long num, int base)
{
    static char Representation[]= "0123456789ABCDEF";
    static char buffer[50];
    char *ptr;
    int negative = 0;

    if (num < 0) {
        negative = 1;
        num = -num;
    }

    ptr = &buffer[49];
    *ptr = '\0';

    do {
        *--ptr = Representation[num % base];
        num /= base;
    } while (num != 0);

    if (negative) {
        *--ptr = '-';
    }

    return(ptr);
}

int vsnprintf(char *restrict buf, size_t size, const char *restrict fmt, va_list args)
{
    char *str, *end;

    if (size > INT_MAX)
        return 0;

    str = buf;
    end = buf + size;

    if (end <= str)
        return 0;

    while (*fmt) {
        if (str >= end - 1)
            break;

        if (*fmt != '%') {
            *str++ = *fmt++;
            continue;
        } else {
            fmt++;
        }

        if (*fmt == '%') {
            *str++ = '%';
            fmt++;
            continue;
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

            case 'd':
            case 'i': {
                fmt++;
                int val = va_arg(args, int);
                str = string(str, convert_signed((long)val, 10), end);
                continue;
            }

            case 'u': {
                fmt++;
                unsigned int val = va_arg(args, unsigned int);
                str = string(str, convert((unsigned long)val, 10), end);
                continue;
            }

            case 'o': {
                fmt++;
                unsigned int val = va_arg(args, unsigned int);
                str = string(str, convert((unsigned long)val, 8), end);
                continue;
            }

            case 'x': {
                fmt++;
                unsigned int val = va_arg(args, unsigned int);
                char *hex_str = convert((unsigned long)val, 16);
                // Convert to lowercase
                char *temp = hex_str;
                while (*temp) {
                    if (*temp >= 'A' && *temp <= 'F') {
                        *temp = *temp - 'A' + 'a';
                    }
                    temp++;
                }
                str = string(str, hex_str, end);
                continue;
            }

            case 'X': {
                fmt++;
                unsigned int val = va_arg(args, unsigned int);
                str = string(str, convert((unsigned long)val, 16), end);
                continue;
            }

            case 'p': {
                fmt++;
                void *ptr = va_arg(args, void *);
                if (ptr == 0) {
                    str = string(str, "(nil)", end);
                } else {
                    str = string(str, "0x", end);
                    str = string(str, convert((unsigned long)(uintptr_t)ptr, 16), end);
                }
                continue;
            }

            case 'l': {
                fmt++;
                if (*fmt == 'd' || *fmt == 'i') {
                    fmt++;
                    long val = va_arg(args, long);
                    str = string(str, convert_signed(val, 10), end);
                } else if (*fmt == 'u') {
                    fmt++;
                    unsigned long val = va_arg(args, unsigned long);
                    str = string(str, convert(val, 10), end);
                } else if (*fmt == 'x') {
                    fmt++;
                    unsigned long val = va_arg(args, unsigned long);
                    char *hex_str = convert(val, 16);
                    char *temp = hex_str;
                    while (*temp) {
                        if (*temp >= 'A' && *temp <= 'F') {
                            *temp = *temp - 'A' + 'a';
                        }
                        temp++;
                    }
                    str = string(str, hex_str, end);
                } else if (*fmt == 'X') {
                    fmt++;
                    unsigned long val = va_arg(args, unsigned long);
                    str = string(str, convert(val, 16), end);
                } else {
                    str = character(str, 'l', end);
                }
                continue;
            }

            default: {
                str = character(str, '%', end);
                str = character(str, *fmt, end);
                fmt++;
                continue;
            }
        }
    }

    if (str < end) {
        *str = '\0';
    } else if (size > 0) {
        *(end - 1) = '\0';
    }

    return str - buf;
}

int sprintf(char *__restrict__ str, const char *__restrict__ fmt, ...)
{
    va_list args;
    int written;

    va_start(args, fmt);
    written = vsnprintf(str, 0xfffful, fmt, args);
    va_end(args);

    return written;
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
