#include <fs/fat32.h>

#include <lilac/fs.h>
#include <lilac/log.h>
#include <lilac/panic.h>
#include <lilac/libc.h>
#include <lilac/timer.h>
#include <lilac/err.h>
#include <drivers/blkdev.h>
#include <mm/kmm.h>
#include <mm/kmalloc.h>

#include "fat_internal.h"

#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"

// Read a directory and return a buffer containing the raw directory entries
ssize_t __fat32_read_dir(struct fat_disk *disk, volatile u8 **buffer, int clst)
{
    ssize_t bytes_read = 0;

    if (clst < 0)
        return -EINVAL;

    while (clst < 0x0FFFFFF8) {
        *buffer = krealloc((void*)*buffer, bytes_read + disk->bytes_per_clst);
        if (!*buffer)
            return -ENOMEM;
        int err = __fat_read_clst(disk, disk->bdev->disk, clst, (void*)*buffer + bytes_read);
        if (err)
            return err;
        clst = fat_value(clst, disk);
        bytes_read += disk->bytes_per_clst;
    }

    return bytes_read;
}


// f_pos is the raw index of the next entry to look at, so listing resumes
// correctly even after entries before it are added or removed
int fat32_readdir(struct file *file, struct dirent *dirp, unsigned int count)
{
    struct inode *inode = file->f_dentry->d_inode;
    struct fat_dir d;
    struct fat_dir_pos pos;
    unsigned int i = 0;

    int err = fat_dir_load(inode, &d);
    if (err)
        return err;

    pos.next = file->f_pos;
    while (i < count && fat_dir_next(&d, &pos)) {
        struct fat_file *e = fat_dir_entry(&d, pos.sfn);
        struct dirent *out = &dirp[i++];

        memset(out, 0, sizeof(*out));
        if (pos.lfn[0])
            strncpy(out->d_name, pos.lfn, sizeof(out->d_name) - 1);
        else
            fat_get_sfn(e, out->d_name);
        out->d_ino = inode->i_ino;
        out->d_off = pos.next;
        out->d_reclen = sizeof(*out);
        out->d_type = (e->attributes & FAT_DIR_ATTR) ? DT_DIR : DT_REG;
    }
    file->f_pos = pos.next;

    fat_dir_put(&d);
    return i;
}

int fat32_mkdir(struct inode *dir, struct dentry *new_dentry, umode_t mode)
{
    return fat_new_entry(dir, new_dentry, FAT_DIR_ATTR);
}
