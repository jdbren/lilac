#include <lilac/device.h>

#include <lilac/fs.h>
#include <lilac/lilac.h>

[[nodiscard]]
int device_register(struct device *dev)
{
    mutex_init(&dev->mutex);
    return 0;
}

int add_device(const char *path, struct file_operations *fops)
{
    int err = 0;
    if ((err = vfs_create(path, 0))) {
        klog(LOG_ERROR, "Failed to create %s with %d\n", path, -err);
        return err;
    }
    struct dentry *dentry = lookup_path(path);
    if (IS_ERR(dentry))
        return PTR_ERR(dentry);

    struct inode *inode = dentry->d_inode;
    if (!inode)
        return -ENOENT;
    inode->i_fop = fops;
    inode->i_type = TYPE_DEV;

    return 0;
}


int dev_mknod(struct inode *parent_dir, struct dentry *node, umode_t mode, dev_t dev)
{
    struct inode *inode = kzmalloc(sizeof(*inode));
    if (!inode)
        return -ENOMEM;
    inode->i_mode = mode;
    inode->i_sb = parent_dir->i_sb;
    inode->i_count = 1;
    inode->i_op = parent_dir->i_op;
    inode->i_type = TYPE_DIR;

    node->d_inode = inode;
    list_add_tail(&inode->i_list, &parent_dir->i_sb->s_inodes);
    hlist_add_head(&node->d_sib, &node->d_parent->d_children);

    return 0;
}
