#include <string.h>

int strcmp(const char *__s1, const char *__s2)
{
    for (; *__s1 == *__s2; ++__s1, ++__s2)
        if (*__s1 == '\0')
            return 0;
    return *__s1 - *__s2;
}

int strncmp(const char *__s1, const char *__s2, size_t __n)
{
    for (; __n && *__s1 == *__s2; --__n, ++__s1, ++__s2)
        if (*__s1 == '\0' || __n == 0)
            return 0;
    return *__s1 - *__s2;
}
