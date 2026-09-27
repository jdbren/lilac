#include <fs/fat32.h>

#include <lilac/fs.h>
#include <lilac/log.h>
#include <lilac/libc.h>
#include <lilac/err.h>
#include <lilac/time.h>
#include <drivers/blkdev.h>
#include <mm/kmalloc.h>

#include "fat_internal.h"

struct inode *fat_alloc_inode(struct super_block *sb)
{
    struct inode *new_node = kzmalloc(sizeof(struct inode));
    if (!new_node) {
        klog(LOG_ERROR, "fat_alloc_inode: Out of memory allocating inode\n");
        return ERR_PTR(-ENOMEM);
    }

    new_node->i_sb = sb;
    new_node->i_op = &fat_iops;
    new_node->i_count = 1;
    new_node->i_mode = 0777;

    return new_node;
}

void fat_destroy_inode(struct inode *inode)
{
    if (inode->i_private)
        kfree(inode->i_private);
    if (inode->i_list.next)
        list_del(&inode->i_list);
    kfree(inode);
}

static int unique_ino(void)
{
    static unsigned long ino = 1;
    return ++ino;
}

static struct inode *fat_iget(struct super_block *sb, u32 dir_clst, u32 dir_idx)
{
    struct inode *tmp;

    list_for_each_entry(tmp, &sb->s_inodes, i_list) {
        struct fat_inode *fi = (struct fat_inode*)tmp->i_private;
        if (fi->dir_clst == dir_clst && fi->dir_idx == dir_idx)
            return tmp;
    }

    return NULL;
}

struct inode *fat_build_inode(struct super_block *sb, struct fat_inode *info)
{
    struct inode *inode;

    inode = fat_iget(sb, info->dir_clst, info->dir_idx);
    if (inode)
        return inode;

    inode = fat_alloc_inode(sb);
    if (IS_ERR(inode)) {
        return inode;
    }

    inode->i_ino = unique_ino();
    inode->i_size = info->entry.file_size;
    inode->i_atime = fat_time_to_unix(info->entry.last_access_date, 0);
    inode->i_mtime = fat_time_to_unix(info->entry.last_write_date, info->entry.last_write_time);
    inode->i_ctime = fat_time_to_unix(info->entry.creation_date, info->entry.creation_time);
    inode->i_private = info;
    inode->i_mode |= info->entry.attributes & FAT_DIR_ATTR ? S_IFDIR : S_IFREG;
    inode->i_nlink = 1;

    list_add_tail(&inode->i_list, &sb->s_inodes);

    return inode;
}

// Return 0 if found, 1 if not found, negative on error
static int fat32_find(struct inode *dir, const char *name,
    struct fat_inode *info)
{
    struct fat_dir d;
    struct fat_dir_pos pos;
    int ret = fat_dir_load(dir, &d);
    if (ret)
        return ret;

    ret = 1;
    if (fat_dir_find(&d, name, &pos)) {
        memcpy(&info->entry, fat_dir_entry(&d, pos.sfn), sizeof(info->entry));
        info->dir_clst = d.first_clst;
        info->dir_idx = pos.sfn;
        ret = 0;
    }
    fat_dir_put(&d);
    return ret;
}

#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"

struct dentry *fat32_lookup(struct inode *parent, struct dentry *find,
    unsigned int flags)
{
    struct inode *inode;
    struct fat_inode *info = kzmalloc(sizeof(*info));
    long err = 0;

    if (!info)
        return ERR_PTR(-ENOMEM);

    if ((err = fat32_find(parent, find->d_name.data, info)) == 0) {
        inode = fat_build_inode(parent->i_sb, info);
        if (IS_ERR(inode)) {
            kfree(info);
            return ERR_CAST(inode);
        }
        if (inode->i_private != info)
            kfree(info); // already cached
        iget(inode);
        find->d_inode = inode;
    } else if (err > 0) {
        kfree(info);
    } else {
        kfree(info);
        return ERR_PTR(err);
    }

    return NULL;
}
