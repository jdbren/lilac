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

int __fat32_read_all_dirent(struct file *file, struct dirent **dirents_ptr)
{
    u32 start_clst;
    struct fat_disk *disk = (struct fat_disk*)file->f_dentry->d_inode->i_sb->private;
    volatile unsigned char *buffer = kvirtual_alloc(disk->bytes_per_clst * 64, PG_WRITE);
    struct dirent *dir_buf;
    u32 num_dirents = disk->bytes_per_clst / sizeof(struct dirent);

    start_clst = __fat_get_clst_num(file, disk);
    if (start_clst <= 0)
        return -1;
    if (__do_fat32_read(file, start_clst, buffer, 64) < 0)
        return -1;

    dir_buf = kzmalloc(disk->bytes_per_clst);
    if (!dir_buf) {
        kvirtual_free((void*)buffer, disk->bytes_per_clst * 64);
        return -ENOMEM;
    }

    u32 i = 0;
    struct fat_file *entry = (struct fat_file*)buffer;
    while (entry->name[0] != 0) {
        if ((u8)entry->name[0] == FAT_UNUSED ||
            entry->name[0] == 0x0 ||
            entry->attributes & FAT_VOL_LABEL ||
            entry->attributes == LONG_FNAME)
        {
            entry++;
            continue;
        }
        if (i >= num_dirents) {
            num_dirents *= 2;
            void *tmp = kcalloc(num_dirents, sizeof(struct dirent));
            if (!tmp) {
                kfree(dir_buf);
                kvirtual_free((void*)buffer, disk->bytes_per_clst * 64);
                return -ENOMEM;
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
        ++entry;
        ++i;
    }

    *dirents_ptr = dir_buf;
#ifdef DEBUG_FAT
    for (int i = 0; i < num_dirents; i++)
        klog(LOG_DEBUG, "dirent %d: %s\n", i, dir_buf[i].d_name);
#endif
    kvirtual_free((void*)buffer, disk->bytes_per_clst * 64);
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

// This is awful, need to rewrite
int fat32_mkdir(struct inode *dir, struct dentry *new, unsigned short mode)
{
    struct fat_file *entry = NULL;
    struct fat_disk *disk = (struct fat_disk*)dir->i_sb->private;
    struct gendisk *hd = dir->i_sb->s_bdev->disk;
    struct fat_file *parent_dir = (struct fat_file*)dir->i_private;
    u32 clst = parent_dir->cl_low + (u32)(parent_dir->cl_high << 16);
    volatile unsigned char *buffer = kzmalloc(disk->bytes_per_clst);
    struct timestamp cur_time = get_timestamp();
    char name[8];
    int ret = 0;

    memset(name, ' ', 8);
    for (int i = 0; i < 8 && isprint(new->d_name[i]); i++)
        name[i] = new->d_name[i] = toupper(new->d_name[i]);

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

        if (entry->name[0] == 0 || (u8)entry->name[0] == FAT_UNUSED)
            break;

        clst = __get_FAT_val(clst, disk);
    }

    if (clst >= 0x0FFFFFF8)
        kerror("Need to allocate new cluster\n");

    int new_clst = __fat_find_free_clst(disk);
    if (new_clst == -1)
        kerror("No free clusters\n");
    disk->FAT.FAT_buf[new_clst - disk->FAT.first_clst] |= 0x0fffffffUL;

    u16 fat_date = FAT_SET_DATE(cur_time.year, cur_time.month, cur_time.day);
    u16 fat_time = FAT_SET_TIME(cur_time.hour, cur_time.minute, cur_time.second);

    if (!entry)
        kerror("entry is NULL\n");

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

    __fat_write_clst(disk, hd, clst, (void*)buffer);

    struct fat_inode *fat_i = kzmalloc(sizeof(struct fat_inode));
    if (!fat_i) {
        ret = -ENOMEM;
        goto error;
    }
    fat_i->entry = *entry;

    new->d_inode = fat_build_inode(dir->i_sb, fat_i);
    new->d_inode->i_type = TYPE_DIR;

    memset((void*)buffer, 0, disk->bytes_per_clst);
    entry = (struct fat_file*)buffer;
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
    entry->cl_low = clst & 0xFFFF;
    entry->cl_high = clst >> 16;
    entry->creation_date = parent_dir->creation_date;
    entry->creation_time = parent_dir->creation_time;
    entry->last_write_date = parent_dir->last_write_date;
    entry->last_write_time = parent_dir->last_write_time;
    entry->last_access_date = parent_dir->last_access_date;

    disk->fs_info.free_clst_cnt--;
    disk->fs_info.next_free_clst = __fat_find_free_clst(disk);
    __fat_write_clst(disk, hd, new_clst, (void*)buffer);
    fat_write_FAT(disk, hd);
    fat32_write_fs_info(disk, hd);

error:
    kfree((void*)buffer);
    return ret;
}
