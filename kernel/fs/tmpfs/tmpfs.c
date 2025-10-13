#include "tmpfs_internal.h"

const struct file_operations tmpfs_fops = {
    .read = tmpfs_read,
    .write = tmpfs_write,
    .readdir = tmpfs_readdir,
    .release = tmpfs_close
};

const struct inode_operations tmpfs_iops = {
    .open = tmpfs_open,
    .lookup = tmpfs_lookup,
    .create = tmpfs_create,
    .mkdir = tmpfs_mkdir
};

const struct super_operations tmpfs_sops = {
    .alloc_inode = tmpfs_alloc_inode,
    .destroy_inode = tmpfs_destroy_inode
};
