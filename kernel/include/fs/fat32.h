// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _FAT32_H
#define _FAT32_H

#include <lilac/types.h>

struct super_block;
struct block_device;
struct inode;
struct dentry;
struct file;
struct dirent;


struct dentry *fat32_lookup(struct inode *parent, struct dentry *find,
    unsigned int flags);
struct dentry *fat32_init(void *dev, struct super_block *sb);
int fat32_open(struct inode *inode, struct file *file);
int fat32_close(struct inode *inode, struct file *file);
ssize_t fat32_read(struct file *file, void *file_buf, size_t count);
ssize_t fat32_write(struct file *file, const void *file_buf, size_t count);
int fat32_readdir(struct file *file, struct dirent *dirp, unsigned int count);
int fat32_mkdir(struct inode *dir, struct dentry *new, unsigned short mode);

#endif
