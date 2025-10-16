#ifndef _FS_UTILS_H
#define _FS_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

int get_basename(char *__restrict dst, const char *__restrict path, size_t size);
int get_dirname(char *__restrict dst, const char *__restrict path, size_t size);
int path_component_len(const char *path, int n_pos);

#ifdef __cplusplus
}
#endif

#endif
