#include <lilac/fs.h>

#include <lilac/log.h>
#include <lilac/kmalloc.h>

#define L1_CACHE_BYTES 64

#define GOLDEN_RATIO_32 0x61C88647UL
#define GOLDEN_RATIO_64 0x61C8864680B583EBULL

#if BITS_PER_LONG == 32
#define GOLDEN_RATIO_NUM GOLDEN_RATIO_32
#elif BITS_PER_LONG == 64
#define GOLDEN_RATIO_NUM GOLDEN_RATIO_64
#endif

static const unsigned long i_hash_shift = 16;
static const unsigned long i_hash_mask = (1 << i_hash_shift) - 1;

static unsigned long hash(struct super_block *sb, unsigned long hashval)
{
    unsigned long tmp;

    tmp = (hashval * (unsigned long)sb) ^ (GOLDEN_RATIO_NUM + hashval) /
            L1_CACHE_BYTES;
    tmp = tmp ^ ((tmp ^ GOLDEN_RATIO_NUM) >> i_hash_shift);
    return tmp & i_hash_mask;
}

void iget(struct inode *inode)
{
    struct super_block *sb = inode->i_sb;

    acquire_lock(&sb->s_lock);

    if (inode->i_count == 0) {
        list_add_tail(&inode->i_list, &sb->s_inodes);
    }

    inode->i_count++;
    release_lock(&sb->s_lock);
}

void iput(struct inode *inode)
{
    struct super_block *sb = inode->i_sb;

    acquire_lock(&sb->s_lock);

    if (inode->i_count > 1) {
        inode->i_count--;
        release_lock(&sb->s_lock);
        return;
    }

    list_del(&inode->i_list);
    release_lock(&sb->s_lock);

    if (sb->s_op->destroy_inode) {
        sb->s_op->destroy_inode(inode);
    } else {
        kfree(inode);
    }
}

int insert_inode(struct super_block *sb, struct inode *inode)
{
    return 0;
}

int release_inode(struct super_block *sb, struct inode *inode)
{
    return 0;
}

struct inode *lookup_inode(struct super_block *sb, unsigned long hashval)
{
    return NULL;
}
