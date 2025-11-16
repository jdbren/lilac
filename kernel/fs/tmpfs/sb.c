#include <lilac/fs.h>
#include <lilac/log.h>
#include <lilac/err.h>
#include "tmpfs_internal.h"

#include <mm/kmalloc.h>

struct inode* tmpfs_alloc_inode(struct super_block *sb)
{
    struct inode *inode = kzmalloc(sizeof(struct inode));
    if (!inode) {
        klog(LOG_ERROR, "tmpfs_alloc_inode: Out of memory allocating inode\n");
        return ERR_PTR(-ENOMEM);
    }

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
    if (!root_dentry) {
        klog(LOG_ERROR, "tmpfs_init: Failed to allocate root dentry\n");
        return ERR_PTR(-ENOMEM);
    }
    struct inode *root_inode = tmpfs_alloc_inode(sb);
    if (IS_ERR_OR_NULL(root_inode)) {
        klog(LOG_ERROR, "tmpfs_init: Failed to allocate root inode\n");
        kfree(root_dentry);
        return ERR_CAST(root_inode);
    }
    struct tmpfs_dir *root_dir = kzmalloc(sizeof(struct tmpfs_dir));
    if (!root_dir) {
        klog(LOG_ERROR, "tmpfs_init: Failed to allocate root tmpfs_dir\n");
        tmpfs_destroy_inode(root_inode);
        kfree(root_dentry);
        return ERR_PTR(-ENOMEM);
    }

    root_inode->i_private = root_dir;
    root_inode->i_mode = S_IFDIR|S_IREAD|S_IWRITE|S_IEXEC;
    root_dentry->d_count = 1;
    root_dentry->d_sb = sb;
    root_dentry->d_inode = root_inode;
    sb->s_root = root_dentry;

    return root_dentry;
}
