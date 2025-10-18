#include <fs/fat32.h>

#include <lilac/fs.h>
#include <lilac/log.h>
#include <lilac/panic.h>
#include <lilac/libc.h>
#include <lilac/timer.h>
#include <lilac/err.h>
#include <drivers/blkdev.h>
#include <lilac/kmm.h>
#include <lilac/kmalloc.h>

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
    struct fat_disk *disk = (struct fat_disk*)file->f_dentry->d_inode->i_sb->private;
    volatile u8 *raw_buf = NULL;
    struct fat_file *entry;
    struct dirent *dir_buf;
    int num_dirents = 8;

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
    entry->name[0] != 0 && (u8*)entry < (u8*)entry + count;
    entry++) {
        if (INVALID_ENTRY(entry) || entry->name[0] == 0x0)
            continue;
        // if (entry->name[0] == 0x0)
        //     break;

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
        int c = 0;
        for (c = 0; c < 8 && entry->name[c] != ' '; c++)
            dir_buf[i].d_name[c] = entry->name[c];
        if (entry->ext[0] != ' ') {
            dir_buf[i].d_name[c++] = '.';
            for (int j = 0; j < 3 && entry->ext[j] != ' '; j++)
                dir_buf[i].d_name[c++] = entry->ext[j];
        }
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
    return i;
}

static void mk_dot_dirs(struct fat_file *entry, struct fat_file *parent, u32 new_clst, u32 p_clst)
{
    struct timestamp cur_time = get_timestamp();
    u16 fat_date = FAT_SET_DATE(cur_time.year, cur_time.month, cur_time.day);
    u16 fat_time = FAT_SET_TIME(cur_time.hour, cur_time.minute, cur_time.second);

    entry->name[0] = '.';
    memset(entry->name + 1, ' ', 7 + 3);
    entry->file_size = 0;
    entry->attributes = FAT_DIR_ATTR;
    entry->cl_low = new_clst & 0xFFFF;
    entry->cl_high = new_clst >> 16;
    entry->creation_date = fat_date;
    entry->creation_time = fat_time;
    entry->last_write_date = fat_date;
    entry->last_write_time = fat_time;
    entry->last_access_date = fat_date;

    entry++;
    entry->name[0] = '.'; entry->name[1] = '.';
    memset(entry->name + 2, ' ', 6 + 3);
    entry->file_size = 0;
    entry->attributes = FAT_DIR_ATTR;
    entry->cl_low = p_clst & 0xFFFF;
    entry->cl_high = p_clst >> 16;
    entry->creation_date = parent->creation_date;
    entry->creation_time = parent->creation_time;
    entry->last_write_date = parent->last_write_date;
    entry->last_write_time = parent->last_write_time;
    entry->last_access_date = parent->last_access_date;
}

static void mk_new_dirent(struct fat_file *entry, u32 new_clst, const char *name)
{
    struct timestamp cur_time = get_timestamp();
    u16 fat_date = FAT_SET_DATE(cur_time.year, cur_time.month, cur_time.day);
    u16 fat_time = FAT_SET_TIME(cur_time.hour, cur_time.minute, cur_time.second);

    memset(entry->name, ' ', 8);
    memset(entry->ext, ' ', 3);
    entry->attributes = FAT_DIR_ATTR;
    entry->cl_low = new_clst & 0xFFFF;
    entry->cl_high = new_clst >> 16;
    entry->file_size = 0;
    entry->creation_date = fat_date;
    entry->creation_time = fat_time;
    entry->last_write_date = fat_date;
    entry->last_write_time = fat_time;
    entry->last_access_date = fat_date;

    strncpy((char*)entry->name, name, 8);
    strncpy((char*)entry->ext, "   ", 3);
}


int fat32_mkdir(struct inode *dir, struct dentry *new_dentry, umode_t mode)
{
    struct fat_file *entry = NULL;
    struct fat_disk *disk = (struct fat_disk*)dir->i_sb->private;
    struct gendisk *hd = dir->i_sb->s_bdev->disk;
    struct fat_file *parent_dir = (struct fat_file*)dir->i_private;
    int clst = parent_dir->cl_low + ((u32)parent_dir->cl_high << 16);
    int prev_clst = clst;
    volatile unsigned char *buffer = kzmalloc(disk->bytes_per_clst);
    char name[8];
    int ret = 0;

    memset(name, ' ', 8);
    for (int i = 0; i < 8 && isprint(new_dentry->d_name[i]); i++)
        name[i] = new_dentry->d_name[i] = toupper(new_dentry->d_name[i]);

    while (clst < 0x0FFFFFF8) {
        __fat_read_clst(disk, hd, clst, (void*)buffer);
        for (entry = (struct fat_file*)buffer;
            entry < (struct fat_file*)(buffer + disk->bytes_per_clst) &&
            !(entry->name[0] == 0 || (u8)entry->name[0] == FAT_UNUSED);
            entry++)
        {
            if (!strncmp((char*)entry->name, name, 8)) {
                klog(LOG_INFO, "Directory %-8s already exists\n", name);
                ret = -1;
                goto error;
            }
        }

        if (entry < (struct fat_file*)(buffer + disk->bytes_per_clst) &&
         (entry->name[0] == 0 || (u8)entry->name[0] == FAT_UNUSED))
            break;
        else
            entry = NULL;

        prev_clst = clst;
        clst = fat_value(clst, disk);
    }

    if (clst >= 0x0FFFFFF8) {
        clst = __fat_find_free_clst(disk);
        if (clst < 0)
            panic("No free clusters\n");
        __fat_add_new_clst(disk, prev_clst, clst);
        klog(LOG_DEBUG, "Added new cluster %x to dir\n", clst);
        __fat_read_clst(disk, hd, clst, (void*)buffer);
        memset((void*)buffer, 0, disk->bytes_per_clst);
        entry = (struct fat_file*)buffer;
    }

    if (!entry) {
        ret = -ENOSPC;
        goto error;
    }

    int new_clst = __fat_find_alloc_clst(disk, 0);
    if (new_clst < 0) {
        ret = -ENOSPC;
        goto error;
    }

    mk_new_dirent(entry, new_clst, name);
    __fat_write_clst(disk, hd, clst, (void*)buffer);

    struct fat_inode *fat_i = kzmalloc(sizeof(struct fat_inode));
    if (!fat_i) {
        ret = -ENOMEM;
        goto error;
    }
    fat_i->entry = *entry;
    new_dentry->d_inode = fat_build_inode(dir->i_sb, fat_i);

    memset((void*)buffer, 0, disk->bytes_per_clst);
    mk_dot_dirs((struct fat_file*)buffer, parent_dir, new_clst, clst);
    __fat_write_clst(disk, hd, new_clst, (void*)buffer);

    fat_write_FAT(disk, hd);
    // fat32_write_fs_info(disk, hd);

error:
    kfree((void*)buffer);
    return ret;
}
