#ifndef _FS_PATH_H
#define _FS_PATH_H

#include <lilac/config.h>

#define PATH_MAX 256
#define NAME_MAX 32

__must_check
char * get_user_path(const char *path);

#endif
