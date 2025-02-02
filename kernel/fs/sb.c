#include <lilac/fs.h>
#include <lilac/err.h>
#include <lilac/sync.h>
#include <mm/kmalloc.h>

struct super_block * alloc_sb(enum fs_type type)
{
    struct super_block *sb = kzmalloc(sizeof(struct super_block));
    if (!sb)
        return ERR_PTR(-ENOMEM);

    sb->s_blocksize = 0x1000;
    sb->s_maxbytes = 0xfffff;
    sb->s_type = type;
    sb->s_count = 1;
    sb->s_active = true;
    sb->s_bdev = NULL;
    sb->s_bdev_file = NULL;
    INIT_LIST_HEAD(&sb->s_inodes);

    return sb;
}

void destroy_sb(struct super_block *sb)
{
    kfree(sb);
}
