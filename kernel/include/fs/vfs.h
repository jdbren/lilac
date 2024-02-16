#ifndef _VFS_H
#define _VFS_H

#include <kernel/types.h>

int open(const char *path, int flags, int mode);
size_t read(int fd, void *buf, size_t count);
size_t write(int fd, const void *buf, size_t count);
int close(int fd);

#endif
