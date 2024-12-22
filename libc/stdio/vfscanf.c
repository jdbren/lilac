#include <stdio.h>

int vfscanf(FILE *__restrict__ stream, const char *__restrict__ format, va_list args)
{
    char *buffer = stream->_ptr;
    int ret = 0;

    while (*format) {
        if (*format == '%') {
            format++;
            if (*format == 'd') {
                int *arg = va_arg(args, int*);
                *arg = 0;
                while (*buffer >= '0' && *buffer <= '9') {
                    *arg = *arg * 10 + *buffer - '0';
                    buffer++;
                }
                ret++;
            }
            else if (*format == 's') {
                char *arg = va_arg(args, char*);
                while (*buffer != ' ' && *buffer != '\0') {
                    *arg = *buffer;
                    arg++;
                    buffer++;
                }
                *arg = '\0';
                ret++;
            }
        }
        else {
            if (*buffer != *format)
                return ret;
            buffer++;
        }
        format++;
    }

    return ret;
}
