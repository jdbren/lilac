#include <fs/fat32.h>

#include <lilac/lilac.h>
#include <lilac/libc.h>
#include <lilac/fs.h>
#include <lilac/file.h>
#include <lilac/timer.h>
#include <drivers/blkdev.h>

#include "fat_internal.h"

int fat32_open(struct inode *inode, struct file *file)
{
    file->f_dentry->d_inode = inode;
    file->f_op = &fat_fops;
    file->f_count++;
    return 0;
}
