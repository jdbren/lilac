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
