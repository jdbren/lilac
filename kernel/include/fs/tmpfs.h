#ifndef _TMPFS_H
#define _TMPFS_H

#include <lilac/types.h>

struct super_block;
struct dentry;
struct file;

struct dentry * tmpfs_init(void *device, struct super_block *sb);
struct file * tmpfs_anon_file(const char *name);

#endif
