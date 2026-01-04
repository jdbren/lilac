#ifndef _FS_UTILS_H
#define _FS_UTILS_H

#include <lilac/types.h>
struct dentry;
extern struct dentry *root_dentry;

int get_basename(char *restrict dst, const char *restrict path, size_t size);
int get_dirname(char *restrict dst, const char *restrict path, size_t size);
int path_component_len(const char *path, int n_pos);
char * build_absolute_path(struct dentry *d);
char * get_user_path(const char *path);

static inline bool is_absolute_path(const char *path)
{
    return path[0] == '/';
}

static inline bool is_relative_path(const char *path)
{
    return path[0] != '/';
}

#endif
