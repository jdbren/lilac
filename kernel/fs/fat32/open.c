#include <fs/fat32.h>

#include <lilac/log.h>
#include <lilac/file.h>

#include "fat_internal.h"

int fat32_open(struct inode *inode, struct file *file)
{
    klog(LOG_DEBUG, "Opening file %s\n", file->f_path);
    file->f_inode = inode;
    file->f_op = &fat_fops;
    file->f_count++;
    return 0;
}
