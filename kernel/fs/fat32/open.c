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
    struct fat_inode *info = (struct fat_inode*)inode->i_private;
    file->f_dentry->d_inode = inode;
    file->f_op = &fat_fops;
    file->f_count++;
    if (inode->i_type == TYPE_DIR) {
        info->buf.num_dirent = __fat32_read_all_dirent(file, &info->buf.dirent);
    }
    return 0;
}

int fat32_create(struct inode *parent, struct dentry *new, umode_t mode)
{
    struct fat_file *entry = NULL;
    struct fat_disk *disk = (struct fat_disk*)parent->i_sb->private;
    struct gendisk *hd = parent->i_sb->s_bdev->disk;
    struct fat_file *parent_dir = (struct fat_file*)parent->i_private;
    u32 clst = parent_dir->cl_low + (u32)(parent_dir->cl_high << 16);
    volatile unsigned char *buffer = kzmalloc(disk->bytes_per_clst);
    struct timestamp cur_time = get_timestamp();
    char name[8];
    int ret = 0;

    memset(name, ' ', 8);
    for (int i = 0; i < 8 && isprint(new->d_name[i]); i++)
        name[i] = toupper(new->d_name[i]);

    while (clst < 0x0FFFFFF8) {
        __fat_read_clst(disk, hd, clst, (void*)buffer);
        for (entry = (struct fat_file*)buffer;
            entry < (struct fat_file*)(buffer + disk->bytes_per_clst) &&
            !(entry->name[0] == 0 || entry->name[0] == FAT_UNUSED);
            entry++)
        {
            if (!strncmp((char*)entry->name, name, 8)) {
                klog(LOG_INFO, "File %-8s already exists\n", name);
                ret = -1;
                goto error;
            }
        }

        if (entry->name[0] == 0 || entry->name[0] == FAT_UNUSED)
            break;

        clst = __get_FAT_val(clst, disk);
    }

    if (clst >= 0x0FFFFFF8)
        kerror("Need to allocate new cluster\n");

    long new_clst = __fat_find_free_clst(disk);
    if (new_clst == -1)
        kerror("No free clusters\n");
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

    strncpy((char*)entry->name, name, 8);
    strncpy((char*)entry->ext, "   ", 3);

    __fat_write_clst(disk, hd, clst, (const void*)buffer);

    struct fat_inode *fat_i = kzmalloc(sizeof(struct fat_inode));
    if (!fat_i) {
        klog(LOG_ERROR, "fat32_create: Out of memory allocating fat_inode\n");
        ret = -ENOMEM;
        goto error;
    }
    fat_i->entry = *entry;

    new->d_inode = fat_build_inode(parent->i_sb, fat_i);
    new->d_inode->i_type = TYPE_FILE;

    disk->fs_info.free_clst_cnt--;
    disk->fs_info.next_free_clst = __fat_find_free_clst(disk);

    memset((void*)buffer, 0, disk->bytes_per_clst);
    __fat_write_clst(disk, hd, new_clst, (const void*)buffer);
    fat_write_FAT(disk, hd);
    fat32_write_fs_info(disk, hd);

error:
    kfree((void*)buffer);
    return ret;
}

int fat32_close(struct inode *inode, struct file *file)
{
    return 0;
}
