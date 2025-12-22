#include <lilac/fs.h>

#include <lilac/log.h>
#include <lilac/err.h>
#include <mm/kmalloc.h>

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

int inode_init(struct super_block *sb, struct inode *inode)
{
    static const struct inode_operations empty_iops;
    static const struct file_operations empty_fops;

    inode->i_sb = sb;
    inode->i_flags = 0;
    atomic_store(&inode->i_count, 1);
    inode->i_op = &empty_iops;
    inode->i_fop = &empty_fops;
    inode->i_nlink = 1;
    inode->i_uid = 0;
    inode->i_gid = 0;
    inode->i_size = 0;
    inode->i_blocks = 0;
    // inode->i_bytes = 0;
    // inode->i_generation = 0;
    inode->i_rdev = 0;
    spin_lock_init(&inode->i_lock);
    mutex_init(&inode->i_mutex);
    inode->i_private = NULL;

    // this_cpu_inc(nr_inodes);

    return 0;
}

struct inode * alloc_inode(struct super_block *sb)
{
    struct inode *inode;
    if (sb->s_op->alloc_inode)
        inode = sb->s_op->alloc_inode(sb);
    else
        inode = kzmalloc(sizeof(struct inode));

    if (!inode)
        return NULL;

    if (unlikely(inode_init(sb, inode))) {
        if (sb->s_op->destroy_inode)
            sb->s_op->destroy_inode(inode);
        else
            kfree(inode);
        return NULL;
    }

    return inode;
}

void destroy_inode(struct inode *inode)
{
    struct super_block *sb = inode->i_sb;

    if (sb->s_op->destroy_inode) {
        sb->s_op->destroy_inode(inode);
    } else {
        kfree(inode);
    }
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
