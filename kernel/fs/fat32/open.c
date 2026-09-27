#include <fs/fat32.h>

#include <lilac/lilac.h>
#include <lilac/libc.h>
#include <lilac/fs.h>
#include <lilac/timer.h>
#include <drivers/blkdev.h>

#include "fat_internal.h"

int fat32_open(struct inode *inode, struct file *file)
{
    struct fat_inode *info = (struct fat_inode*)inode->i_private;
    file->f_op = &fat_fops;
    info->open_count++;
    if (info->entry.attributes & FAT_DIR_ATTR)
        info->buf.num_dirent = __fat32_read_all_dirent(file, &info->buf.dirent);
    return 0;
}

int fat32_create(struct inode *parent, struct dentry *new, umode_t mode)
{
    return fat_new_entry(parent, new, 0);
}

int fat32_close(struct inode *inode, struct file *file)
{
    struct fat_inode *info = (struct fat_inode*)inode->i_private;
    if (info->open_count && --info->open_count == 0 && info->unlinked)
        fat_release_clusters(inode);
    return 0;
}
