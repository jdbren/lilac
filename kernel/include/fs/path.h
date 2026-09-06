#ifndef _FS_PATH_H
#define _FS_PATH_H

#include <lilac/config.h>
#include <lilac/libc.h>

__must_check
char * get_user_path(const char *path);

static inline void nd_terminate_link(void *name, size_t len, size_t maxlen)
{
    ((char *) name)[MIN(len, maxlen)] = '\0';
}

#endif
