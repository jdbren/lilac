#include <lilac/libc.h>
#include <lilac/err.h>
#include <lilac/fs.h>
#include <lilac/kmalloc.h>
#include <lilac/uaccess.h>

/**
 * dirname - extract directory part of a pathname into dst buffer.
 * @dst: destination buffer to store result (null-terminated).
 * @path: input pathname (immutable).
 * @size: size of dst buffer.
 *
 * Returns 0 on success, or negative errno on failure.
 * Follows POSIX dirname semantics.
 */
int get_dirname(char *restrict dst, const char *restrict path, size_t size)
{
    const char *p;
    size_t len;
    ptrdiff_t dlen;

    if (!dst || !path || size == 0)
        return -EINVAL;

    len = strlen(path);

    /* Strip trailing slashes (leave one if root) */
    while (len > 1 && path[len - 1] == '/')
        len--;

    /* Handle root "/" */
    if (len == 1 && path[0] == '/') {
        if (size < 2)
            return -ENAMETOOLONG;
        strcpy(dst, "/");
        return 0;
    }

    /* Find last slash in trimmed path */
    p = strrchr(path, '/');
    if (!p || p > path + len - 1) {
        /* No slash => current directory */
        if (size < 2)
            return -ENAMETOOLONG;
        strcpy(dst, ".");
        return 0;
    }

    /* Move back over duplicate slashes */
    while (p > path && *(p - 1) == '/')
        p--;

    /* If slash is leading, result is "/" */
    if (p == path) {
        if (size < 2)
            return -ENAMETOOLONG;
        strcpy(dst, "/");
        return 0;
    }

    /* Copy directory part up to p */
    dlen = p - path;
    if ((size_t)dlen + 1 > size)
        return -ENAMETOOLONG;
    memcpy(dst, path, dlen);
    dst[dlen] = '\0';
    return 0;
}

/**
 * basename - extract filename part of a pathname into dst buffer.
 * @dst: destination buffer to store result (null-terminated).
 * @path: input pathname (immutable).
 * @size: size of dst buffer.
 *
 * Returns 0 on success, or negative errno on failure.
 * Follows POSIX basename semantics.
 */
int get_basename(char *restrict dst, const char *restrict path, size_t size)
{
    const char *p;
    size_t len;
    size_t slen;

    if (!dst || !path || size == 0)
        return -EINVAL;

    len = strlen(path);

    /* Strip trailing slashes (leave one if root) */
    while (len > 1 && path[len - 1] == '/')
        len--;

    /* Handle root "/" */
    if (len == 1 && path[0] == '/') {
        if (size < 2)
            return -ENAMETOOLONG;
        strcpy(dst, "/");
        return 0;
    }

    /* Find last slash in trimmed path */
    p = strrchr(path, '/');
    if (!p || p > path + len - 1) {
        /* No slash => everything is basename */
        slen = len;
    } else {
        slen = (path + len) - (p + 1);
    }

    /* Copy filename part */
    if (slen + 1 > size)
        return -ENAMETOOLONG;
    memcpy(dst, p ? p + 1 : path, slen);
    dst[slen] = '\0';
    return 0;
}

int path_component_len(const char *path, int n_pos)
{
    int i = 0;
    while (path[n_pos] != '/' && path[n_pos] != '\0') {
        i++; n_pos++;
    }
    return i;
}

char * get_user_path(const char *path)
{
    char *kpath = kmalloc(PATH_MAX);
    if (!kpath)
        return ERR_PTR(-ENOMEM);

    long err = strncpy_from_user(kpath, path, PATH_MAX);
    if (err < 0) {
        kfree(kpath);
        return ERR_PTR(err);
    } else if (err == PATH_MAX) {
        kfree(kpath);
        return ERR_PTR(-ENAMETOOLONG);
    }
    return kpath;
}
