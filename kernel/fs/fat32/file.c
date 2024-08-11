#include <fs/fat32.h>

#include <lilac/fs.h>
#include <lilac/file.h>
#include <lilac/log.h>
#include <lilac/panic.h>
#include <drivers/blkdev.h>
#include <mm/kmm.h>

#include <string.h>

#include "internal.h"

// Read from a file into a buffer (always multiples of cluster size)
int __do_fat32_read(const struct file *file, u32 clst, volatile u8 *buffer,
    size_t num_clst)
{
    if (num_clst == 0)
        return -1;

    size_t clst_read = 0;
    const struct inode *inode = file->f_inode;
    const struct gendisk *gd = inode->i_sb->s_bdev->disk;
    struct fat_disk *fat_disk = (struct fat_disk*)inode->i_sb->private;

    while (clst < 0x0FFFFFF8 && clst_read < num_clst) {
        __fat_read_clst(fat_disk, gd, clst, buffer);
        clst = __get_FAT_val(clst, fat_disk);
        buffer += fat_disk->bytes_per_clst;
        clst_read++;
    }

    return 0;
}

// Write to a file from a buffer (always multiples of cluster size)
int __do_fat32_write(const struct file *file, u32 clst,
    const u8 *buffer, size_t num_clst)
{
    if (num_clst == 0)
        return -1;

    size_t clst_writ = 0;
    u32 next_val;
    const struct inode *inode = file->f_inode;
    const struct gendisk *gd = inode->i_sb->s_bdev->disk;
    struct fat_disk *fat_disk = (struct fat_disk*)inode->i_sb->private;

    while (clst_writ < num_clst) {
        __fat_write_clst(fat_disk, gd, clst, buffer);
        clst_writ++;

        clst = __get_FAT_val(clst, fat_disk);
        if (clst >= 0x0FFFFFF8 && clst_writ < num_clst) {
            next_val = __fat_find_free_clst(fat_disk);
            if (next_val == -1)
                kerror("No free clusters\n");
            fat_disk->FAT.buf[clst] &= 0xF0000000;
            fat_disk->FAT.buf[clst] |= next_val & 0x0FFFFFFF;
            fat_disk->FAT.buf[next_val] |= 0x0FFFFFFF;
            clst = next_val;
        }

        buffer += fat_disk->bytes_per_clst;
    }

    return 0;
}


ssize_t fat32_read(struct file *file, void *file_buf, size_t count)
{
    ssize_t bytes_read;
    u32 start_clst;
    struct fat_disk *disk = (struct fat_disk*)file->f_inode->i_sb->private;
    struct fat_file *fat_file = (struct fat_file*)file->f_inode->i_private;
    u32 offset = file->f_pos % disk->bytes_per_clst;
    u32 num_clst = ROUND_UP(count + offset, disk->bytes_per_clst) /
        disk->bytes_per_clst;
    volatile unsigned char *buffer = kvirtual_alloc(disk->bytes_per_clst * num_clst, PG_WRITE);

    start_clst = __fat_get_clst_num(file, disk);
    if (start_clst == 0)
        goto out;

    bytes_read = __do_fat32_read(file, start_clst, buffer, num_clst);
    if (bytes_read < 0)
        goto out;

    if (file->f_pos + count > fat_file->file_size)
        count = fat_file->file_size - file->f_pos;
    memcpy(file_buf, buffer + offset, count);
    file->f_pos += count;
    bytes_read = count;

out:
    kvirtual_free(buffer, disk->bytes_per_clst * num_clst);
    return bytes_read;
}

ssize_t fat32_write(struct file *file, const void *file_buf, size_t count)
{
    ssize_t bytes_written;
    u32 start_clst;
    struct fat_disk *disk = (struct fat_disk*)file->f_inode->i_sb->private;
    struct fat_file *fat_file = (struct fat_file*)file->f_inode->i_private;
    u32 offset = file->f_pos % disk->bytes_per_clst;
    u32 num_clst = ROUND_UP(count + offset, disk->bytes_per_clst) /
        disk->bytes_per_clst;
    unsigned char *buffer = kvirtual_alloc(disk->bytes_per_clst * num_clst, PG_WRITE);

    start_clst = __fat_get_clst_num(file, disk);
    if (start_clst == 0)
        goto out;

    if (offset) {
        if(__do_fat32_read(file, start_clst, buffer, 1) < 0)
            goto out;
    }

    if (file->f_pos + count > fat_file->file_size)
        fat_file->file_size = file->f_pos + count;
    memcpy(buffer + offset, file_buf, count);

    bytes_written = __do_fat32_write(file, start_clst, buffer, num_clst);
    if (bytes_written < 0)
        goto out;

    file->f_pos += count;
    bytes_written = count;

out:
    kvirtual_free(buffer, disk->bytes_per_clst * num_clst);
    return bytes_written;
}
