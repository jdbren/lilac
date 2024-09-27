#include "internal.h"
#include <lilac/file.h>

const struct file_operations tmpfs_fops = {
    .read = tmpfs_read,
    .write = tmpfs_write,
    .readdir = tmpfs_readdir
};

const struct inode_operations tmpfs_iops = {
    .open = tmpfs_open,
    .lookup = tmpfs_lookup,
    .create = tmpfs_create,
    .mkdir = tmpfs_mkdir
};
