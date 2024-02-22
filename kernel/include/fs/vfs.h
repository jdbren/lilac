// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _VFS_H
#define _VFS_H

#include <stdbool.h>
#include <kernel/types.h>

enum fs_type {
    FAT
};

void vfs_install_disk(int type, bool boot);

int open(const char *path, int flags, int mode);
size_t read(int fd, void *buf, size_t count);
size_t write(int fd, const void *buf, size_t count);
int close(int fd);

#endif
