#include <lilac/fs.h>
#include "tmpfs_internal.h"

#include <mm/kmalloc.h>

struct inode* tmpfs_alloc_inode(struct super_block *sb)
{
    struct inode *inode = kzmalloc(sizeof(struct inode));

    inode->i_sb = sb;
    inode->i_op = &tmpfs_iops;
    inode->i_count = 1;
    inode->i_type = TYPE_DIR;
    list_add_tail(&inode->i_list, &sb->s_inodes);

    return inode;
}

void tmpfs_destroy_inode(struct inode *inode)
{
    kfree(inode);
}

struct dentry* tmpfs_init(void *device, struct super_block *sb)
{
    sb->s_op = &tmpfs_sops;
    sb->s_blocksize = 0x1000;
    sb->s_maxbytes = 0xfffff;
    INIT_LIST_HEAD(&sb->s_inodes);

    struct dentry *root_dentry = kzmalloc(sizeof(struct dentry));
    struct inode *root_inode = tmpfs_alloc_inode(sb);
    struct tmpfs_dir *root_dir = kzmalloc(sizeof(struct tmpfs_dir));

    root_inode->i_private = root_dir;

    root_dentry->d_sb = sb;
    root_dentry->d_inode = root_inode;

    return root_dentry;
}

const struct super_operations tmpfs_sops = {
    .alloc_inode = tmpfs_alloc_inode,
    .destroy_inode = tmpfs_destroy_inode
};
