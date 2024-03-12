#include <string.h>

char *strcat(char *restrict dest, const char *restrict src)
{
    char *ret = dest;
    while (*dest)
        dest++;
    while ((*dest++ = *src++))
        ;
    return ret;
}

char *strncat(char *restrict dest, const char *restrict src, size_t n)
{
    char *ret = dest;
    while (*dest)
        dest++;
    while (n-- && (*dest++ = *src++))
        ;
    *dest = '\0';
    return ret;
}
