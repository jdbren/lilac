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
        __fat_read_clst(disk, disk->bdev->disk, clst, (void*)*buffer + bytes_read);
        clst = fat_value(clst, disk);
        bytes_read += disk->bytes_per_clst;
    }

    return bytes_read;
}


int __fat32_read_all_dirent(struct file *file, struct dirent **dirents_ptr)
{
    int i = 0;
    int count = 0;
    struct fat_disk *disk = (struct fat_disk*)file->f_dentry->d_inode->i_sb->s_fs_info;
    volatile u8 *raw_buf = NULL;
    struct fat_file *entry;
    struct dirent *dir_buf;
    int num_dirents = 8;
    char lfn[FAT_LFN_BUF];

    memset(lfn, 0, sizeof(lfn));

    if ((count = __fat32_read_dir(disk, &raw_buf, __fat_get_clst_num(file, disk))) <= 0) {
        klog(LOG_ERROR, "__fat32_read_all_dirent: Failed to read directory data\n");
        return count;
    }

    dir_buf = kzmalloc(num_dirents * sizeof(struct dirent));
    if (!dir_buf) {
        i = -ENOMEM;
        goto out;
    }

    for (entry = (struct fat_file*)raw_buf;
            entry->name[0] != 0 && (u8*)entry < (u8*)raw_buf + count;
            entry++) {

        if (entry->name[0] == FAT_UNUSED) {
            memset(lfn, 0, sizeof(lfn));
            continue;
        }

        if (entry->attributes == LONG_FNAME) {
            fat_get_lfn_part(entry, lfn);
            continue;
        }

        if (INVALID_ENTRY(entry) || entry->name[0] == 0x0) {
            memset(lfn, 0, sizeof(lfn));
            continue;
        }

        if (i >= num_dirents) {
            num_dirents *= 2;
            void *tmp = kcalloc(num_dirents, sizeof(struct dirent));
            if (!tmp) {
                kfree(dir_buf);
                i = -ENOMEM;
                goto out;
            }
            memcpy(tmp, dir_buf, i * sizeof(struct dirent));
            kfree(dir_buf);
            dir_buf = tmp;
        }

        if (lfn[0]) {
            strncpy(dir_buf[i].d_name, lfn, sizeof(dir_buf[i].d_name) - 1);
            dir_buf[i].d_name[sizeof(dir_buf[i].d_name) - 1] = '\0';
        } else {
            fat_get_sfn(entry, dir_buf[i].d_name);
        }
        memset(lfn, 0, sizeof(lfn));

        dir_buf[i].d_ino = file->f_dentry->d_inode->i_ino;
        dir_buf[i].d_reclen = sizeof(struct dirent);
        dir_buf[i].d_off = file->f_pos + i;
        dir_buf[i].d_type = (entry->attributes & FAT_DIR_ATTR) ? DT_DIR : DT_REG;
        ++i;
    }

    *dirents_ptr = dir_buf;
#ifdef DEBUG_FAT
    for (int i = 0; i < num_dirents; i++)
        klog(LOG_DEBUG, "dirent %d: %s\n", i, dir_buf[i].d_name);
#endif
out:
    kfree((void*)raw_buf);
    return i;
}

int fat32_readdir(struct file *file, struct dirent *dir_buf, unsigned int count)
{
    struct fat_inode *info = (struct fat_inode*)file->f_dentry->d_inode->i_private;
    u32 i = 0;
    while (i < count && i + file->f_pos < info->buf.num_dirent) {
        dir_buf[i] = info->buf.dirent[i + file->f_pos];
        i++;
    }
    file->f_pos += i;
    return i;
}

int fat32_mkdir(struct inode *dir, struct dentry *new_dentry, umode_t mode)
{
    return fat_new_entry(dir, new_dentry, FAT_DIR_ATTR);
}
