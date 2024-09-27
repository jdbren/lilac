#include <string.h>
#include <stdint.h>

int get_basename(char *restrict dst, const char *restrict path)
{
    char *p = strrchr(path, '/');
    if (p)
        strcpy(dst, p + 1);
    return p ? 0 : -1;
}

int get_dirname(char *restrict dst, const char *restrict path)
{
    char *p = strrchr(path, '/');
    if (p)
        strncpy(dst, path, (uintptr_t)p - (uintptr_t)path);
    return p ? 0 : -1;
}
