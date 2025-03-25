#include <string.h>
#if defined(__is_libk)
#include <mm/kmalloc.h>
#endif

char *strdup(const char *str)
{
#if defined(__is_libk)
    size_t len = strlen(str) + 1;
    char *new = kmalloc(len);
    if (!new)
        return NULL;
    return strncpy(new, str, len);
#else
    return NULL;
#endif
}
