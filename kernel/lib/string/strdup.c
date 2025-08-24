#include <lilac/libc.h>
#include <lilac/kmalloc.h>

char *strdup(const char *str)
{
    size_t len = strlen(str) + 1;
    char *new = kmalloc(len);
    if (!new)
        return NULL;
    return strncpy(new, str, len);
}

char *strndup(const char *str, size_t n)
{
    size_t len = strnlen(str, n);
    char *new = kmalloc(len + 1);
    if (!new)
        return NULL;
    strncpy(new, str, len);
    new[len] = '\0';
    return new;
}
