#include "ext2.h"

#include <lilac/fs.h>
#include <drivers/blkdev.h>

struct super_block *ext2_read_super(struct block_device *bdev, struct super_block *sb)
{
    int sector_size = bdev->disk->sector_size;
    u64 sb_lba = bdev->first_sector_lba + (1024 / sector_size);
    u64 sb_offset = 1024 % sector_size;
}
