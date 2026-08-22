#include <fs/fat32.h>

#include <lilac/fs.h>
#include <lilac/lilac.h>
#include <lilac/libc.h>
#include <lilac/err.h>
#include <drivers/blkdev.h>
#include <mm/kmm.h>
#include <mm/page.h>

#include "fat_internal.h"

static u32 fat32_get_or_alloc_clst_num(struct file *file, struct fat_disk *disk)
{
    struct fat_file *fat_file = (struct fat_file*)file->f_dentry->d_inode->i_private;
    u32 clst_num = fat_clst_value(fat_file);
    u32 clst_off = file->f_pos / disk->bytes_per_clst;

    if (clst_num == 0) {
        clst_num = __fat_find_alloc_clst(disk, 0);
        if (clst_num == 0)
            return 0;
        fat_file->cl_low = clst_num & 0xFFFF;
        fat_file->cl_high = clst_num >> 16;
    }

    while (clst_off--) {
        u32 next_clst = fat_value(clst_num, disk);
        if (next_clst >= 0x0FFFFFF8U) {
            next_clst = __fat_find_alloc_clst(disk, clst_num);
            if (next_clst == 0)
                return 0;
        }
        clst_num = next_clst;
    }

    return clst_num;
}

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
    ssize_t bytes_written = -EIO;
    u32 start_clst;
    struct fat_disk *disk = (struct fat_disk*)file->f_dentry->d_inode->i_sb->s_fs_info;
    struct fat_file *fat_file = (struct fat_file*)file->f_dentry->d_inode->i_private;
    u32 offset = file->f_pos % disk->bytes_per_clst;
    u32 num_clst = ROUND_UP(count + offset, disk->bytes_per_clst) /
        disk->bytes_per_clst;

    if (!count)
        return 0;

    unsigned char *buffer = get_free_pages(PAGE_UP_COUNT(disk->bytes_per_clst * num_clst), 0);
    if (!buffer)
        return -ENOMEM;

    start_clst = fat32_get_or_alloc_clst_num(file, disk);
    if (start_clst == 0) {
        klog(LOG_ERROR, "fat32_write: no space to map cluster for %s (pos=%lu len=%lu)\n",
            file->f_dentry ? file->f_dentry->d_name.data : "<unknown>", file->f_pos, count);
        bytes_written = -ENOSPC;
        goto out;
    }

    if (offset) {
        if (__do_fat32_read(file, start_clst, buffer, 1) < 0) {
            klog(LOG_ERROR, "fat32_write: failed pre-read for partial write %s (pos=%lu)\n",
                file->f_dentry ? file->f_dentry->d_name.data : "<unknown>", file->f_pos);
            bytes_written = -EIO;
            goto out;
        }
    }

    memcpy(buffer + offset, file_buf, count);

    bytes_written = __do_fat32_write(file, start_clst, buffer, num_clst);
    if (bytes_written < 0) {
        klog(LOG_ERROR, "fat32_write: write failed for %s (pos=%lu len=%lu err=%ld)\n",
            file->f_dentry ? file->f_dentry->d_name.data : "<unknown>", file->f_pos,
            count, bytes_written);
        goto out;
    }
    bytes_written = count;

    if (file->f_pos + count > fat_file->file_size) {
        fat_file->file_size = file->f_pos + count;
        file->f_dentry->d_inode->i_size = fat_file->file_size;
    }

    // if (fat_write_FAT(disk, file->f_dentry->d_inode->i_sb->s_bdev->disk) < 0) {
    //     klog(LOG_ERROR, "fat32_write: failed to persist FAT for %s\n",
    //         file->f_dentry ? file->f_dentry->d_name : "<unknown>");
    //     bytes_written = -EIO;
    // }

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
        return -EINVAL;

    size_t clst_writ = 0;
    const struct inode *inode = file->f_dentry->d_inode;
    struct gendisk *gd = inode->i_sb->s_bdev->disk;
    struct fat_disk *fat_disk = (struct fat_disk*)inode->i_sb->s_fs_info;

    if (clst < fat_disk->FAT.first_clst || clst > fat_disk->FAT.last_clst) {
        klog(LOG_ERROR, "__do_fat32_write: invalid start cluster %u\n", clst);
        return -EINVAL;
    }

    while (clst_writ < num_clst) {
        __fat_write_clst(fat_disk, gd, clst, buffer);
        clst_writ++;

        u32 next_clst = fat_value(clst, fat_disk);
        if (next_clst >= 0x0FFFFFF8U && clst_writ < num_clst) {
            next_clst = __fat_find_alloc_clst(fat_disk, clst);
            if (next_clst == 0) {
                klog(LOG_ERROR, "__do_fat32_write: out of clusters while extending chain\n");
                return -ENOSPC;
            }
        }
        clst = next_clst;
        buffer += fat_disk->bytes_per_clst;
    }

    return 0;
}
