#include <lilac/libc.h>
#include <mm/kmalloc.h>

char *strdup(const char *str)
{
    size_t len = strlen(str) + 1;
    char *new = kmalloc(len);
    if (!new)
        return NULL;
    return strncpy(new, str, len);
}
