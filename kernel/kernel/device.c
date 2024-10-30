#include <lilac/device.h>

#include <lilac/fs.h>
#include <lilac/log.h>
#include <lilac/panic.h>

int __must_check device_register(struct device *dev)
{
    mutex_init(&dev->mutex);

}

int add_device(const char *path, struct file_operations *fops)
{
    if (create(path, 0)) {
        klog(LOG_ERROR, "Failed to create %s\n", path);
        return -1;
    }
    struct dentry *dentry = lookup_path(path);
    if (!dentry)
        return -1;

    struct inode *inode = dentry->d_inode;
    inode->i_fop = fops;
    inode->i_type = TYPE_DEV;

    return 0;
}


int dev_mknod(struct inode *parent_dir, struct dentry *node, umode_t mode, dev_t dev)
{
    struct inode *inode = kzmalloc(sizeof(*inode));
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
