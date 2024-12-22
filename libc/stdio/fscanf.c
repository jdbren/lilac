#include <stdio.h>

int fscanf(FILE *__restrict__ stream, const char *__restrict__ format, ...)
{
    va_list args;
    va_start(args, format);
    int ret = vfscanf(stream, format, args);
    va_end(args);
    return ret;
}
