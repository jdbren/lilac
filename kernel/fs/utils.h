#ifndef _FS_UTILS_H
#define _FS_UTILS_H

int get_basename(char *restrict dst, const char *restrict path, size_t size);
int get_dirname(char *restrict dst, const char *restrict path, size_t size);
int path_component_len(const char *path, int n_pos);

#endif
