#include <stdio.h>

#include <stdarg.h>

int sscanf(const char *restrict buffer, const char *restrict format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vsscanf(buffer, format, args);
    va_end(args);
    return ret;
}
