#ifndef _FS_PATH_H
#define _FS_PATH_H

#include <lilac/config.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif
#ifndef NAME_MAX
#define NAME_MAX 32
#endif

__must_check
char * get_user_path(const char *path);

#endif
