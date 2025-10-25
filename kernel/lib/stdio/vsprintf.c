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
    char *str = buf;
    char *end;

    if (size == 0) {
        end = buf; /* no space, but we still compute length */
    } else {
        end = buf + size;
    }

    if (end <= str)
        return 0;

    while (*fmt) {
        if (*fmt != '%') {
            if (str < end - 1)
                *str++ = *fmt;
            else if (str < end) /* room only for null later */
                *str++ = *fmt; /* allow filling last char; final null handled below */
            fmt++;
            continue;
        }
        fmt++; /* skip '%' */

        /* Parse flags */
        bool left = false;
        bool plus = false;
        bool spaceflag = false;
        bool hash = false;
        bool zero = false;
        for (;;) {
            if (*fmt == '-') { left = true; fmt++; continue; }
            if (*fmt == '+') { plus = true; fmt++; continue; }
            if (*fmt == ' ') { spaceflag = true; fmt++; continue; }
            if (*fmt == '#') { hash = true; fmt++; continue; }
            if (*fmt == '0') { zero = true; fmt++; continue; }
            break;
        }

        /* Width */
        int width = -1;
        if (*fmt == '*') {
            width = va_arg(args, int);
            if (width < 0) { left = true; width = -width; }
            fmt++;
        } else {
            if (*fmt >= '0' && *fmt <= '9') {
                width = 0;
                while (*fmt >= '0' && *fmt <= '9') {
                    width = width * 10 + (*fmt - '0');
                    fmt++;
                }
            }
        }

        /* Precision */
        int precision = -1;
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') {
                precision = va_arg(args, int);
                fmt++;
            } else {
                precision = 0;
                while (*fmt >= '0' && *fmt <= '9') {
                    precision = precision * 10 + (*fmt - '0');
                    fmt++;
                }
            }
            if (precision < 0) precision = 0;
        }

        /* Length modifier */
        enum { LEN_NONE, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_Z } length = LEN_NONE;
        if (*fmt == 'h') {
            fmt++;
            if (*fmt == 'h') { length = LEN_HH; fmt++; } else length = LEN_H;
        } else if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') { length = LEN_LL; fmt++; } else length = LEN_L;
        } else if (*fmt == 'z') {
            length = LEN_Z;
            fmt++;
        }

        /* Conversion */
        char conv = *fmt ? *fmt++ : '\0';

        /* temporary output buffer */
        char outbuf[64];
        char *out = outbuf;
        size_t outlen = 0;
        char sign_char = 0;
        bool is_num = false;
        bool uppercase = false;

        switch (conv) {
            case 'c': {
                char ch = (char)va_arg(args, int);
                outbuf[0] = ch;
                outlen = 1;
                break;
            }

            case 's': {
                const char *s = va_arg(args, const char *);
                if (!s) s = "(null)";
                /* apply precision (max chars) */
                const char *p = s;
                size_t len = 0;
                while (*p && (precision < 0 || (int)len < precision)) { p++; len++; }
                if (len >= sizeof(outbuf)) len = sizeof(outbuf) - 1;
                memcpy(outbuf, s, len);
                outlen = len;
                break;
            }

            case 'd':
            case 'i': {
                is_num = true;
                long long val;
                if (length == LEN_LL) val = va_arg(args, long long);
                else if (length == LEN_L) val = va_arg(args, long);
                else val = va_arg(args, int);

                if (val < 0) {
                    sign_char = '-';
                    val = -val;
                } else if (plus) {
                    sign_char = '+';
                } else if (spaceflag) {
                    sign_char = ' ';
                }

                /* convert number to string (decimal) */
                char tmp[64];
                int tp = 0;
                if (val == 0) {
                    tmp[tp++] = '0';
                } else {
                    unsigned long long u = (unsigned long long)val;
                    while (u) {
                        tmp[tp++] = '0' + (u % 10);
                        u /= 10;
                    }
                }
                /* apply precision (minimum digits) */
                while (tp < precision) tmp[tp++] = '0';
                /* reverse into outbuf */
                outlen = tp;
                for (int i = 0; i < tp; i++) outbuf[i] = tmp[tp - 1 - i];
                break;
            }

            case 'u':
            case 'o':
            case 'x':
            case 'X': {
                is_num = true;
                int base = 10;
                if (conv == 'o') base = 8;
                else if (conv == 'x' || conv == 'X') base = 16;
                uppercase = (conv == 'X');

                unsigned long long val;
                if (length == LEN_LL) val = va_arg(args, unsigned long long);
                else if (length == LEN_L) val = va_arg(args, unsigned long);
                else val = va_arg(args, unsigned int);

                /* convert unsigned */
                char tmp[64];
                int tp = 0;
                if (val == 0) {
                    tmp[tp++] = '0';
                } else {
                    while (val) {
                        int d = val % base;
                        tmp[tp++] = (d < 10) ? ('0' + d) : ((uppercase ? 'A' : 'a') + (d - 10));
                        val /= base;
                    }
                }
                /* precision -> minimum digits */
                while (tp < precision) tmp[tp++] = '0';

                /* prefix for alternate form */
                if (hash && base == 8 && tmp[tp - 1] != '0') { tmp[tp++] = '0'; }
                if (hash && base == 16) {
                    /* will handle prefix when emitting */
                }

                outlen = tp;
                for (int i = 0; i < tp; i++) outbuf[i] = tmp[tp - 1 - i];
                break;
            }

            case 'p': {
                void *ptr = va_arg(args, void *);
                if (!ptr) {
                    const char *nil = "(nil)";
                    size_t l = strlen(nil);
                    if (l >= sizeof(outbuf)) l = sizeof(outbuf)-1;
                    memcpy(outbuf, nil, l);
                    outlen = l;
                } else {
                    unsigned long long val = (unsigned long long)(uintptr_t)ptr;
                    char tmp[32];
                    int tp = 0;
                    if (val == 0) tmp[tp++] = '0';
                    else {
                        while (val) {
                            int d = val % 16;
                            tmp[tp++] = '0' + (d < 10 ? d : (d - 10 + 'a' - '0'));
                        }
                    }
                    /* "0x" prefix */
                    outbuf[0] = '0';
                    outbuf[1] = 'x';
                    outlen = 2;
                    for (int i = 0; i < tp && outlen < (int)sizeof(outbuf); i++) {
                        outbuf[outlen++] = tmp[tp - 1 - i];
                    }
                }
                break;
            }

            case '%': {
                outbuf[0] = '%';
                outlen = 1;
                break;
            }

            default: {
                /* Unknown specifier: output it literally */
                outbuf[0] = '%';
                outbuf[1] = conv ? conv : '%';
                outlen = conv ? 2 : 1;
                break;
            }
        }

        /* Prepare final emission, applying numeric prefixes and padding */
        size_t total_len = outlen;
        size_t prefix_len = 0;
        char prefix[3] = {0};

        if (is_num) {
            if (sign_char) {
                prefix[0] = sign_char;
                prefix_len = 1;
            }
            if (hash) {
                if (conv == 'x' || conv == 'X') {
                    if (outlen > 0 && !(outlen == 1 && outbuf[0] == '0')) {
                        prefix[prefix_len++] = '0';
                        prefix[prefix_len++] = (conv == 'X') ? 'X' : 'x';
                    }
                }
                /* octal handled earlier by forcing a leading 0 in digits */
            }
            total_len = prefix_len + outlen;
        }

        int pad = 0;
        if (width > 0 && (int)total_len < width)
            pad = width - (int)total_len;

        char padchar = (zero && !left && precision < 0) ? '0' : ' ';

        /* If padchar is '0' and there is a sign/prefix, emit sign first then zeros */
        if (!left) {
            if (padchar == '0') {
                /* emit prefix/sign before zeros */
                for (size_t i = 0; i < prefix_len; i++) {
                    if (str < end - 1) *str++ = prefix[i];
                    else if (str < end) *str++ = prefix[i];
                }
                for (int i = 0; i < pad; i++) {
                    if (str < end - 1) *str++ = padchar;
                    else if (str < end) *str++ = padchar;
                }
            } else {
                for (int i = 0; i < pad; i++) {
                    if (str < end - 1) *str++ = padchar;
                    else if (str < end) *str++ = padchar;
                }
                for (size_t i = 0; i < prefix_len; i++) {
                    if (str < end - 1) *str++ = prefix[i];
                    else if (str < end) *str++ = prefix[i];
                }
            }
        } else {
            /* left: emit prefix first */
            for (size_t i = 0; i < prefix_len; i++) {
                if (str < end - 1) *str++ = prefix[i];
                else if (str < end) *str++ = prefix[i];
            }
        }

        /* emit main buffer */
        for (size_t i = 0; i < outlen; i++) {
            if (str < end - 1) *str++ = outbuf[i];
            else if (str < end) *str++ = outbuf[i];
        }

        /* trailing pad for left-justified */
        if (left) {
            for (int i = 0; i < pad; i++) {
                if (str < end - 1) *str++ = ' ';
                else if (str < end) *str++ = ' ';
            }
        }
    }

    /* Null terminate */
    if (size > 0) {
        if (str < end) *str = '\0';
        else *(end - 1) = '\0';
    }

    return (int)(str - buf);
}

int vsprintf(char *__restrict buf, size_t size, const char *__restrict fmt, va_list args)
{
    return vsnprintf(buf, 0xfffful, fmt, args);
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

int snprintf(char *__restrict buf, size_t size, const char *__restrict fmt, ...)
{
    va_list args;
    int written;

    va_start(args, fmt);
    written = vsnprintf(buf, size, fmt, args);
    va_end(args);

    return written;
}
