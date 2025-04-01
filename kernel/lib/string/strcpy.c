#include <stddef.h>

char *strcpy(char *restrict dest, const char *restrict src)
{
    char *ret = dest;
    while ((*dest++ = *src++))
        ;
    return ret;
}

char *strncpy(char *restrict dest, const char *restrict src, size_t n)
{
    char *ret = dest;
    while (n-- && (*dest++ = *src++))
        ;
    return ret;
}
