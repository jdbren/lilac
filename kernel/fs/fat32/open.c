#include <fs/fat32.h>

#include <lilac/lilac.h>
#include <lilac/libc.h>
#include <lilac/fs.h>
#include <lilac/timer.h>
#include <drivers/blkdev.h>

#include "fat_internal.h"

int fat32_open(struct inode *inode, struct file *file)
{
    struct fat_inode *info = (struct fat_inode*)inode->i_private;
    file->f_op = &fat_fops;
    if (info->entry.attributes & FAT_DIR_ATTR)
        info->buf.num_dirent = __fat32_read_all_dirent(file, &info->buf.dirent);
    return 0;
}

int fat32_create(struct inode *parent, struct dentry *new, umode_t mode)
{
    struct fat_file *entry = NULL;
    struct fat_disk *disk = (struct fat_disk*)parent->i_sb->s_fs_info;
    struct gendisk *hd = parent->i_sb->s_bdev->disk;
    struct fat_file *parent_dir = (struct fat_file*)parent->i_private;
    u32 clst = parent_dir->cl_low + (u32)(parent_dir->cl_high << 16);
    volatile unsigned char *buffer = kzmalloc(disk->bytes_per_clst);
    struct timestamp cur_time = get_timestamp();
    unsigned char sfn[11];
    int ret = 0;
    u32 prev_clst = 0;

    if (!buffer)
        return -ENOMEM;

    u8 case_flags = fat_make_sfn(new->d_name.data, sfn);

    while (clst < 0x0FFFFFF8) {
        __fat_read_clst(disk, hd, clst, (void*)buffer);
        for (entry = (struct fat_file*)buffer;
            entry < (struct fat_file*)(buffer + disk->bytes_per_clst) &&
            !(entry->name[0] == 0 || (u8)entry->name[0] == FAT_UNUSED);
            entry++)
        {
            if (!memcmp(entry->name, sfn, 11)) {
                klog(LOG_INFO, "File %s already exists\n", new->d_name.data);
                ret = -EEXIST;
                goto error;
            }
        }

        if (entry < (struct fat_file*)(buffer + disk->bytes_per_clst))
            break;

        prev_clst = clst;
        clst = fat_value(clst, disk);
    }

    if (clst >= 0x0FFFFFF8) {
        // Directory is full
        clst = __fat_find_alloc_clst(disk, prev_clst);
        if (!clst) {
            ret = -ENOSPC;
            goto error;
        }
        memset((void*)buffer, 0, disk->bytes_per_clst);
        entry = (struct fat_file*)buffer;
    }
    if (!entry) {
        ret = -EIO;
        goto error;
    }

    long new_clst = __fat_find_free_clst(disk);
    if (new_clst <= 0) {
        ret = -ENOSPC;
        goto error;
    }
    disk->FAT.FAT_buf[new_clst - disk->FAT.first_clst] |= 0x0fffffffUL;

    u16 fat_date = FAT_SET_DATE(cur_time.year, cur_time.month, cur_time.day);
    u16 fat_time = FAT_SET_TIME(cur_time.hour, cur_time.minute, cur_time.second);

    entry->attributes = 0;
    entry->cl_low = new_clst & 0xFFFF;
    entry->cl_high = new_clst >> 16;
    entry->file_size = 0;
    entry->creation_date = fat_date;
    entry->creation_time = fat_time;
    entry->last_write_date = fat_date;
    entry->last_write_time = fat_time;
    entry->last_access_date = fat_date;

    memcpy(entry->name, sfn, 8);
    memcpy(entry->ext, sfn + 8, 3);
    entry->reserved = case_flags;

    __fat_write_clst(disk, hd, clst, (const void*)buffer);

    struct fat_inode *fat_i = kzmalloc(sizeof(struct fat_inode));
    if (!fat_i) {
        klog(LOG_ERROR, "fat32_create: Out of memory allocating fat_inode\n");
        ret = -ENOMEM;
        goto error;
    }
    fat_i->entry = *entry;

    new->d_inode = fat_build_inode(parent->i_sb, fat_i);

    disk->fs_info.free_clst_cnt--;
    disk->fs_info.next_free_clst = __fat_find_free_clst(disk);

    memset((void*)buffer, 0, disk->bytes_per_clst);
    __fat_write_clst(disk, hd, new_clst, (const void*)buffer);
    fat_write_FAT(disk, hd);
    // fat32_write_fs_info(disk, hd);

error:
    kfree((void*)buffer);
    return ret;
}

int fat32_close(struct inode *inode, struct file *file)
{
    return 0;
}
