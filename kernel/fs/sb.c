#include <lilac/fs.h>
#include <lilac/err.h>
#include <lilac/sync.h>
#include <drivers/blkdev.h>
#include <lilac/kmalloc.h>

struct super_block * alloc_sb(struct block_device *bdev)
{
    struct super_block *sb = kzmalloc(sizeof(struct super_block));
    if (!sb)
        return ERR_PTR(-ENOMEM);

    sb->s_blocksize = 0x1000;
    sb->s_maxbytes = 0xfffff;
    sb->s_type = bdev->type;
    sb->s_count = 1;
    sb->s_active = true;
    sb->s_bdev = bdev;
    sb->s_bdev_file = NULL;
    INIT_LIST_HEAD(&sb->s_inodes);

    return sb;
}

void destroy_sb(struct super_block *sb)
{
    kfree(sb);
}
