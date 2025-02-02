#include <lilac/fs.h>
#include <lilac/log.h>
#include "tmpfs_internal.h"

#include <mm/kmalloc.h>

struct inode* tmpfs_alloc_inode(struct super_block *sb)
{
    struct inode *inode = kzmalloc(sizeof(struct inode));

    inode->i_sb = sb;
    inode->i_op = &tmpfs_iops;
    inode->i_count = 1;
    list_add_tail(&inode->i_list, &sb->s_inodes);

    return inode;
}

void tmpfs_destroy_inode(struct inode *inode)
{
    if (inode->i_private)
        kfree(inode->i_private);
    kfree(inode);
}

struct dentry* tmpfs_init(void *device, struct super_block *sb)
{
    klog(LOG_DEBUG, "Initializing tmpfs\n");
    sb->s_op = &tmpfs_sops;
    sb->s_blocksize = 0x1000;
    sb->s_maxbytes = 0xfffff;

    struct dentry *root_dentry = kzmalloc(sizeof(struct dentry));
    struct inode *root_inode = tmpfs_alloc_inode(sb);
    struct tmpfs_dir *root_dir = kzmalloc(sizeof(struct tmpfs_dir));

    root_inode->i_private = root_dir;
    root_inode->i_type = TYPE_DIR;

    root_dentry->d_count = 1;
    root_dentry->d_sb = sb;
    root_dentry->d_inode = root_inode;
    sb->s_root = root_dentry;

    return root_dentry;
}
