#include "ext2.h"

#include <lilac/err.h>
#include <lilac/libc.h>

int ext2_file_open(struct inode *inode, struct file *file)
{
    file->f_op = &ext2_file_fops;
    return 0;
}

int ext2_file_release(struct inode *inode, struct file *file)
{
    return 0;
}

ssize_t ext2_read(struct file *file, void *buf, size_t count)
{
    struct inode *inode = file->f_inode;
    struct super_block *sb = inode->i_sb;
    struct blkio_desc *bio;
    ssize_t bytes_read = 0;
    off_t pos = file->f_pos;
    u32 block;
    if (!buf || count == 0)
        return 0;

    u32 iblock_index = pos / EXT2_BLOCK_SIZE(sb);
    while (bytes_read < count && pos < inode->i_size) {
        long error = ext2_get_block(inode, iblock_index, &block);
        if (error)
            return error;

        size_t block_offset = pos % EXT2_BLOCK_SIZE(sb);
        size_t bytes_to_copy = MIN(count - bytes_read, EXT2_BLOCK_SIZE(sb) - block_offset);
        bytes_to_copy = MIN(bytes_to_copy, inode->i_size - pos);

        bio = sb_bread(sb, block);
        if (IS_ERR(bio))
            return PTR_ERR(bio);
        memcpy((u8*)buf + bytes_read, bio->b_data + block_offset, bytes_to_copy);
        brelease(bio);

        bytes_read += bytes_to_copy;
        pos += bytes_to_copy;
        iblock_index++;
    }

    return bytes_read;
}

const struct inode_operations ext2_file_iops = {
    .open = ext2_file_open,
};

const struct file_operations ext2_file_fops = {
    .read = ext2_read,
    .release = ext2_file_release,
};
