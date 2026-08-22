#ifndef _FS_EXT2_H
#define _FS_EXT2_H

#include <lilac/types.h>

struct super_block;
struct dentry;

struct dentry * ext2_init(void *device, struct super_block *sb);

#endif
