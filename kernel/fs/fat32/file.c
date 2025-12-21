#include <fs/fat32.h>

#include <lilac/fs.h>
#include <lilac/lilac.h>
#include <lilac/libc.h>
#include <drivers/blkdev.h>
#include <mm/kmm.h>
#include <mm/page.h>

#include "fat_internal.h"

ssize_t fat32_read(struct file *file, void *file_buf, size_t count)
{
    ssize_t bytes_read = -1;
    u32 start_clst;
    struct fat_disk *disk = (struct fat_disk*)file->f_dentry->d_inode->i_sb->s_fs_info;
    struct fat_file *fat_file = (struct fat_file*)file->f_dentry->d_inode->i_private;
    if (fat_file->cl_low == 0 || file->f_pos >= fat_file->file_size)
        return 0;
    u32 offset = file->f_pos % disk->bytes_per_clst;
#ifdef DEBUG_FAT
    klog(LOG_DEBUG, "Fat file size: %u, f_pos: %lu, offset: %u\n",
        fat_file->file_size, file->f_pos, offset);
#endif
    count = MIN(count, fat_file->file_size - file->f_pos);

    u32 num_clst = ROUND_UP(count + offset, disk->bytes_per_clst) /
        disk->bytes_per_clst;
    volatile unsigned char *buffer = get_free_pages(PAGE_UP_COUNT(disk->bytes_per_clst * num_clst), 0);

    start_clst = __fat_get_clst_num(file, disk);
    if (start_clst == 0)
        goto out;

    if (__do_fat32_read(file, start_clst, buffer, num_clst))
        goto out;

    memcpy(file_buf, (void*)(buffer + offset), count);
    bytes_read = count;

out:
    free_pages((void*)buffer, PAGE_UP_COUNT(disk->bytes_per_clst * num_clst));
    return bytes_read;
}

ssize_t fat32_write(struct file *file, const void *file_buf, size_t count)
{
    ssize_t bytes_written = -1;
    u32 start_clst;
    struct fat_disk *disk = (struct fat_disk*)file->f_dentry->d_inode->i_sb->s_fs_info;
    struct fat_file *fat_file = (struct fat_file*)file->f_dentry->d_inode->i_private;
    u32 offset = file->f_pos % disk->bytes_per_clst;
    u32 num_clst = ROUND_UP(count + offset, disk->bytes_per_clst) /
        disk->bytes_per_clst;
    unsigned char *buffer = get_free_pages(PAGE_UP_COUNT(disk->bytes_per_clst * num_clst), 0);

    start_clst = __fat_get_clst_num(file, disk);
    if (start_clst == 0)
        goto out;

    if (offset) {
        if(__do_fat32_read(file, start_clst, buffer, 1) < 0)
            goto out;
    }

    memcpy(buffer + offset, file_buf, count);

    bytes_written = __do_fat32_write(file, start_clst, buffer, num_clst);
    if (bytes_written < 0)
        goto out;
    bytes_written = count;

    if (file->f_pos + count > fat_file->file_size) {
        fat_file->file_size = file->f_pos + count;
        file->f_dentry->d_inode->i_size = fat_file->file_size;
    }

out:
    free_pages((void*)buffer, PAGE_UP_COUNT(disk->bytes_per_clst * num_clst));
    return bytes_written;
}

// Read from a file into a buffer (always multiples of cluster size)
int __do_fat32_read(const struct file *file, u32 clst, volatile u8 *buffer,
    size_t num_clst)
{
    if (num_clst == 0)
        return -1;

    size_t clst_read = 0;
    const struct inode *inode = file->f_dentry->d_inode;
    struct gendisk *gd = inode->i_sb->s_bdev->disk;
    struct fat_disk *fat_disk = (struct fat_disk*)inode->i_sb->s_fs_info;

    while (clst < 0x0FFFFFF8 && clst_read < num_clst) {
        __fat_read_clst(fat_disk, gd, clst, (void*)buffer);
        clst = fat_value(clst, fat_disk);
        buffer += fat_disk->bytes_per_clst;
        clst_read++;
    }

    return 0;
}

// Write to a file from a buffer (always multiples of cluster size)
int __do_fat32_write(const struct file *file, u32 clst, const u8 *buffer,
    size_t num_clst)
{
    if (num_clst == 0)
        return -1;

    size_t clst_writ = 0;
    long next_val;
    const struct inode *inode = file->f_dentry->d_inode;
    struct gendisk *gd = inode->i_sb->s_bdev->disk;
    struct fat_disk *fat_disk = (struct fat_disk*)inode->i_sb->s_fs_info;

    while (clst_writ < num_clst) {
        __fat_write_clst(fat_disk, gd, clst, buffer);
        clst_writ++;

        clst = fat_value(clst, fat_disk);
        if (clst >= 0x0FFFFFF8 && clst_writ < num_clst) {
            clst = __fat_find_alloc_clst(fat_disk, clst);
            if (clst <= 0)
                kerror("No free clusters\n");
        }

        buffer += fat_disk->bytes_per_clst;
    }

    return 0;
}
