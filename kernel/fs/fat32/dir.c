#include <fs/fat32.h>

#include <lilac/fs.h>
#include <lilac/log.h>
#include <lilac/panic.h>
#include <drivers/blkdev.h>
#include <mm/kmm.h>
#include <mm/kmalloc.h>

#include <string.h>
#include <ctype.h>

#include "internal.h"

// count is the size of the buffer
int fat32_readdir(struct file *file, struct dirent *dir_buf, unsigned int count)
{
    int num_dirents = count / sizeof(struct dirent);
    u32 start_clst;
    struct fat_disk *disk = (struct fat_disk*)file->f_inode->i_sb->private;
    u32 offset = file->f_pos % disk->bytes_per_clst;
    u32 num_clst = ROUND_UP(count + offset, disk->bytes_per_clst) /
        disk->bytes_per_clst;
    volatile unsigned char *buffer = kvirtual_alloc(disk->bytes_per_clst * num_clst, PG_WRITE);

    start_clst = __fat_get_clst_num(file, disk);
    if (start_clst <= 0)
        return -1;
    if (__do_fat32_read(file, start_clst, buffer, num_clst) < 0)
        return -1;

    int i = 0;
    struct fat_file *entry = (struct fat_file*)buffer;
    while (i < num_dirents && entry->name[0] != 0) {
        if (entry->name[0] == FAT_UNUSED ||
            entry->attributes & FAT_VOL_LABEL ||
            entry->attributes == LONG_FNAME)
        {
            entry++;
            continue;
        }
        strncpy(dir_buf->d_name, entry->name, 8);
        entry++;
        dir_buf++;
        i++;
    }

    kvirtual_free(buffer, disk->bytes_per_clst * num_clst);
    return i;
}

int fat32_mkdir(struct inode *dir, struct dentry *new, unsigned short mode)
{
    struct fat_file *entry;
    struct fat_disk *disk = (struct fat_disk*)dir->i_sb->private;
    struct gendisk *hd = dir->i_sb->s_bdev->disk;
    struct fat_file *parent_dir = (struct fat_file*)dir->i_private;
    u32 clst = parent_dir->cl_low + (u32)(parent_dir->cl_high << 16);
    volatile unsigned char *buffer = kzmalloc(disk->bytes_per_clst);
    char name[8];
    int ret = 0;

    memset(name, ' ', 8);
    for (int i = 0; i < 8 && isprint(new->d_name[i]); i++)
        name[i] = toupper(new->d_name[i]);

    while (clst < 0x0FFFFFF8) {
        __fat_read_clst(disk, hd, clst, buffer);
        for (entry = (struct fat_file*)buffer;
            entry < (struct fat_file*)(buffer + disk->bytes_per_clst) &&
            !(entry->name[0] == 0 || entry->name[0] == 0xe5);
            entry++)
        {
            if (!strncmp(entry->name, name, 8)) {
                klog(LOG_INFO, "Directory %-8s already exists\n", name);
                ret = -1;
                goto error;
            }
        }

        if (entry->name[0] == 0 || entry->name[0] == 0xe5)
            break;

        clst = __get_FAT_val(clst, disk);
    }

    if (clst >= 0x0FFFFFF8)
        kerror("Need to allocate new cluster\n");

    u32 new_clst = __fat_find_free_clst(disk);
    if (new_clst == -1)
        kerror("No free clusters\n");
    disk->FAT.buf[new_clst] |= 0x0fffffffUL;

    entry->attributes = FAT_DIR_ATTR;
    entry->cl_low = new_clst & 0xFFFF;
    entry->cl_high = new_clst >> 16;
    entry->file_size = 0;
    entry->creation_date = FAT_SET_DATE(2024, 9, 5);
    entry->creation_time = FAT_SET_TIME(12, 23, 0);
    entry->last_write_date = FAT_SET_DATE(2024, 9, 5);
    entry->last_write_time = FAT_SET_TIME(12, 23, 0);

    strncpy(entry->name, name, 8);
    strncpy(entry->ext, "   ", 3);

    __fat_write_clst(disk, hd, clst, buffer);

    new->d_inode = fat_build_inode(dir->i_sb, entry);
    new->d_inode->i_type = TYPE_DIR;

    memset(buffer, 0, disk->bytes_per_clst);
    entry = (struct fat_file*)buffer;
    entry->name[0] = '.';
    memset(entry->name + 1, ' ', 7 + 3);
    entry->file_size = 0;
    entry->attributes = FAT_DIR_ATTR;
    entry->cl_low = new_clst & 0xFFFF;
    entry->cl_high = new_clst >> 16;
    entry->creation_date = FAT_SET_DATE(2024, 9, 5);
    entry->creation_time = FAT_SET_TIME(12, 23, 0);
    entry->last_write_date = FAT_SET_DATE(2024, 9, 5);
    entry->last_write_time = FAT_SET_TIME(12, 23, 0);

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

    disk->fs_info.free_clst_cnt--;
    disk->fs_info.next_free_clst = __fat_find_free_clst(disk);
    __fat_write_clst(disk, hd, new_clst, buffer);
    fat_write_FAT(disk, hd);
    fat32_write_fs_info(disk, hd);

error:
    kfree(buffer);
    return ret;
}
