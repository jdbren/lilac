#include <stddef.h>

char *strchr(const char *str, int c)
{
    while (*str) {
        if (*str == c)
            return (char *)str;
        str++;
    }
    return NULL;
}

char *strrchr(const char *str, int c)
{
    const char *last = NULL;
    while (*str) {
        if (*str == c)
            last = str;
        str++;
    }
    return (char *)last;
}
