#include <lilac/libc.h>
#include <lilac/err.h>

int get_basename(char *restrict dst, const char *restrict path, size_t size)
{
    // if (!strcmp(path, "/")) {
    //     if (size < 2) // At least need space for "/\0"
    //         return -1;
    //     dst[0] = '/';
    //     dst[1] = '\0';
    //     return 0;
    // }
    size_t len;
    char *p = strrchr(path, '/');
    if (p) {
        len = strlen(p + 1) + 1;
        if (len >= size)
            return -ENAMETOOLONG;
        strncpy(dst, p + 1, len);
    }
    return p ? 0 : -1;
}

int get_dirname(char *restrict dst, const char *restrict path, size_t size)
{
    if (!strcmp(path, "/")) {
        if (size < 2) // At least need space for "/\0"
            return -1;
        dst[0] = '/';
        dst[1] = '\0';
        return 0;
    }
    size_t len;
    char *p = strrchr(path, '/');
    if (p) {
        len = (uintptr_t)p - (uintptr_t)path;
        if (len >= size)
            return -ENAMETOOLONG;
        strncpy(dst, path, len);
        dst[len] = '\0';
    }
    return p ? 0 : -1;
}

int path_component_len(const char *path, int n_pos)
{
    int i = 0;
    while (path[n_pos] != '/' && path[n_pos] != '\0') {
        i++; n_pos++;
    }
    return i;
}
