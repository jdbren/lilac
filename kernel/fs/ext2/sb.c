#include "ext2.h"

#include <drivers/blkdev.h>
#include <lilac/fs.h>
#include <lilac/lilac.h>
#include <lilac/sync.h>
#include <lib/log_base2.h>

#define BLOCK_SIZE 1024

struct inode * ext2_alloc_inode(struct super_block *sb);
void ext2_destroy_inode(struct inode *inode);
void ext2_put_super(struct super_block *sb);

const struct super_operations ext2_sops = {
    .alloc_inode = ext2_alloc_inode,
    .destroy_inode = ext2_destroy_inode,
    .put_super = ext2_put_super,
};

void ext2_error(struct super_block *sb, const char *function,
        const char *fmt, ...)
{
    va_list args;
    struct ext2_sb_info *sbi = EXT2_SB(sb);
    struct ext2_sb *es = sbi->s_es;
    char *strbuf = kmalloc(1024);

    if (!sb_rdonly(sb)) {
        acquire_lock(&sbi->s_lock);
        sbi->s_mount_state |= EXT2_ERROR_FS;
        es->s_state |= cpu_to_le16(EXT2_ERROR_FS);
        release_lock(&sbi->s_lock);
        // ext2_sync_super(sb, es, 1);
    }

    va_start(args, fmt);
    if (strbuf)
        vsnprintf(strbuf, 1024, fmt, args);
    va_end(args);

    klog(LOG_ERROR, "EXT2-fs (%p): error: %s: %s\n",
           sb, function, strbuf ? strbuf : "(error)");
    kfree(strbuf);

/*
    if (test_opt(sb, ERRORS_PANIC))
        panic("EXT2-fs: panic from previous error\n");
    if (!sb_rdonly(sb) && test_opt(sb, ERRORS_RO)) {
        ext2_msg(sb, KERN_CRIT,
                 "error: remounting filesystem read-only");
        sb->s_flags |= SB_RDONLY;
    }
*/
}

void ext2_msg(struct super_block *sb, const char *prefix,
        const char *fmt, ...)
{
    va_list args;
    char *strbuf = kmalloc(1024);

    va_start(args, fmt);
    if (strbuf)
        vsnprintf(strbuf, 1024, fmt, args);
    va_end(args);

    klog(LOG_INFO, "%sEXT2-fs (%p): %s\n", prefix, sb, strbuf ? strbuf : "(error)");
    kfree(strbuf);
}

static unsigned long descriptor_loc(struct super_block *sb,
                    unsigned long logic_sb_block,
                    int nr)
{
    struct ext2_sb_info *sbi = EXT2_SB(sb);
    unsigned long bg, first_meta_bg;

    first_meta_bg = le32_to_cpu(sbi->s_es->s_first_meta_bg);

    if (!EXT2_HAS_INCOMPAT_FEATURE(sb, EXT2_FEATURE_INCOMPAT_META_BG) ||
        nr < first_meta_bg)
        return (logic_sb_block + nr + 1);
    bg = sbi->s_desc_per_block * nr;

    return ext2_group_first_block_no(sb, bg) + ext2_bg_has_super(sb, bg);
}

static unsigned long long ext2_max_file_size(unsigned long blocksize)
{
    if (blocksize == 1024)
        return (16ull << 30) - 1; // 16 GiB
    else if (blocksize == 2048)
        return (256ull << 30) - 1; // 256 GiB
    else if (blocksize == 4096)
        return (2048ull << 30) - 1; // 2 TiB
    else
        return 0;
}

static struct super_block *
ext2_read_super(struct block_device *bdev, struct super_block *sb)
{
    struct ext2_sb *ext2_sb_disk;
    struct ext2_sb_info *ext2_sb;
    struct blkio_desc *bio;
    const unsigned long sb_block = 1;
    unsigned long logic_sb_block;
    unsigned long offset = 0;
    int blocksize = BLOCK_SIZE;

    ext2_sb = kzmalloc(sizeof(struct ext2_sb_info));
    if (!ext2_sb) {
        klog(LOG_ERROR, "Out of memory allocating ext2 superblock\n");
        sb = ERR_PTR(-ENOMEM);
        goto error;
    }

    sb->s_bdev = bdev;
    ext2_sb->s_sb_block = sb_block;

    blocksize = sb_min_blocksize(sb, BLOCK_SIZE);
    if (!blocksize) {
        ext2_msg(sb, KERN_ERR, "error: unable to set blocksize");
        goto free_ext2_sb;
    }

    /*
     * If the superblock doesn't start on a hardware sector boundary,
     * calculate the offset.
     */
    if (blocksize != BLOCK_SIZE) {
        logic_sb_block = (sb_block * BLOCK_SIZE) / blocksize;
        offset = (sb_block * BLOCK_SIZE) % blocksize;
    } else {
        logic_sb_block = sb_block;
    }

    bio = bread(bdev, logic_sb_block, sb->s_blocksize);
    if (IS_ERR_OR_NULL(bio)) {
        klog(LOG_ERROR, "Failed to read ext2 superblock from disk\n");
        sb = bio ? ERR_CAST(bio) : ERR_PTR(-EIO);
        goto free_ext2_sb;
    }

    ext2_sb_disk = (struct ext2_sb *)(bio->b_data + offset);
    if (ext2_sb_disk->s_magic != EXT2_SUPER_MAGIC) {
        klog(LOG_ERROR, "Invalid ext2 superblock magic: 0x%X\n", ext2_sb_disk->s_magic);
        sb = ERR_PTR(-EINVAL);
        goto free_bio;
    }

    blocksize = BLOCK_SIZE << ext2_sb_disk->s_log_block_size;

    /* If the blocksize doesn't match, re-read the thing.. */
    if (sb->s_blocksize != blocksize) {
        bdrop(bio);

        if (!sb_set_blocksize(sb, blocksize)) {
            ext2_msg(sb, KERN_ERR,
                "error: bad blocksize %d", blocksize);
            goto free_ext2_sb;
        }

        logic_sb_block = (sb_block*BLOCK_SIZE) / blocksize;
        offset = (sb_block*BLOCK_SIZE) % blocksize;
        bio = bread(bdev, logic_sb_block, sb->s_blocksize);
        if (!bio) {
            ext2_msg(sb, KERN_ERR, "error: couldn't read"
                "superblock on 2nd try");
            goto free_ext2_sb;
        }
        ext2_sb_disk = (struct ext2_sb*) (bio->b_data + offset);
        ext2_sb->s_es = ext2_sb_disk;
        if (ext2_sb_disk->s_magic != cpu_to_le16(EXT2_SUPER_MAGIC)) {
            ext2_msg(sb, KERN_ERR, "error: magic mismatch");
            goto free_bio;
        }
    }

    ext2_sb->s_sbh = bio;
    ext2_sb->s_mount_state = ext2_sb_disk->s_state;
    ext2_sb->s_es = ext2_sb_disk;
    sb->s_fs_info = ext2_sb;

    /* fields below depend on blocksize/inode-size; compute those first */
    ext2_sb->s_inode_size = (ext2_sb_disk->s_rev_level == EXT2_DYNAMIC_REV) ?
        ext2_sb_disk->s_inode_size : 128;
    ext2_sb->s_first_ino = (ext2_sb_disk->s_rev_level == EXT2_DYNAMIC_REV) ?
        ext2_sb_disk->s_first_ino : EXT2_GOOD_OLD_FIRST_INO;

    ext2_sb->s_frag_size = 1024 << ext2_sb_disk->s_log_frag_size;
    ext2_sb->s_frags_per_block = ext2_sb->s_frag_size / sb->s_blocksize;
    ext2_sb->s_inodes_per_block = sb->s_blocksize / EXT2_INODE_SIZE(sb);
    ext2_sb->s_frags_per_group = ext2_sb_disk->s_frags_per_group;
    ext2_sb->s_blocks_per_group = ext2_sb_disk->s_blocks_per_group;
    ext2_sb->s_inodes_per_group = ext2_sb_disk->s_inodes_per_group;
    ext2_sb->s_itb_per_group = ext2_sb->s_inodes_per_group /
        ext2_sb->s_inodes_per_block;
    ext2_sb->s_desc_per_block = sb->s_blocksize / sizeof(struct ext2_group_desc);
    ext2_sb->s_addr_per_block_bits = ilog2(sb->s_blocksize / sizeof(u32));
    ext2_sb->s_desc_per_block_bits = ilog2(ext2_sb->s_desc_per_block);
    ext2_sb->s_groups_count = ((ext2_sb_disk->s_blocks_count -
        ext2_sb_disk->s_first_data_block - 1) / EXT2_BLOCKS_PER_GROUP(sb)) + 1;
    ext2_sb->s_gdb_count = (ext2_sb->s_groups_count + EXT2_DESC_PER_BLOCK(sb) - 1) /
        EXT2_DESC_PER_BLOCK(sb);
    ext2_sb->s_group_desc = kcalloc(ext2_sb->s_gdb_count, sizeof(struct blkio_desc *));
    if (!ext2_sb->s_group_desc) {
        klog(LOG_ERROR, "Out of memory allocating group descriptor array\n");
        sb = ERR_PTR(-ENOMEM);
        goto free_bio;
    }

    for (int i = 0; i < ext2_sb->s_gdb_count; i++) {
        unsigned long block = descriptor_loc(sb, logic_sb_block, i);
        ext2_sb->s_group_desc[i] = sb_bread(sb, block);
        if (IS_ERR_OR_NULL(ext2_sb->s_group_desc[i])) {
            for (int j = 0; j < i; j++)
                bdrop(ext2_sb->s_group_desc[j]);
            ext2_error(sb, __func__, "unable to read group descriptors");
            sb = ext2_sb->s_group_desc[i]
                ? ERR_CAST(ext2_sb->s_group_desc[i])
                : ERR_PTR(-EIO);
            goto free_ext2_group_desc;
        }
    }

    memcpy(bdev->name, ext2_sb_disk->s_volume_name,
        MIN(sizeof(ext2_sb_disk->s_volume_name), sizeof(bdev->name)));
    sb->s_maxbytes = ext2_max_file_size(sb->s_blocksize);
    sb->s_type = EXT2;
    sb->s_op = &ext2_sops;
    sem_init(&sb->s_umount, 1);
    sb->s_count = 1;
    sb->s_active = true;
    spin_lock_init(&sb->s_lock);
    INIT_LIST_HEAD(&sb->s_inodes);

    return sb;

free_ext2_group_desc:
    kfree(ext2_sb->s_group_desc);
free_bio:
    bdrop(bio);
free_ext2_sb:
    kfree(ext2_sb);
error:
    return sb;
}

void ext2_put_super(struct super_block *sb)
{
    int db_count;
    int i;
    struct ext2_sb_info *sbi = EXT2_SB(sb);

    // ext2_quota_off_umount(sb);

    // ext2_xattr_destroy_cache(sbi->s_ea_block_cache);
    // sbi->s_ea_block_cache = NULL;

    if (!sb_rdonly(sb)) {
        struct ext2_sb *es = sbi->s_es;
        acquire_lock(&sbi->s_lock);
        es->s_state = cpu_to_le16(sbi->s_mount_state);
        release_lock(&sbi->s_lock);
        // ext2_sync_super(sb, es, 1);
    }
    db_count = sbi->s_gdb_count;
    for (i = 0; i < db_count; i++)
        brelease(sbi->s_group_desc[i]);
    kfree(sbi->s_group_desc);
    // kfree(sbi->s_debts);
    // percpu_counter_destroy(&sbi->s_freeblocks_counter);
    // percpu_counter_destroy(&sbi->s_freeinodes_counter);
    // percpu_counter_destroy(&sbi->s_dirs_counter);
    brelease(sbi->s_sbh);
    sb->s_fs_info = NULL;
    // kfree(sbi->s_blockgroup_lock);
    // fs_put_dax(sbi->s_daxdev, NULL);
    kfree(sbi);
}

struct dentry * ext2_init(void *dev, struct super_block *sb)
{
    struct block_device *bdev = (struct block_device*)dev;
    struct inode *root_inode;
    struct dentry *root_dentry;
    struct super_block *ret;

    ret = ext2_read_super(bdev, sb);
    if (IS_ERR(ret))
        return ERR_CAST(ret);

    sb->s_flags |= SB_RDONLY;

    root_inode = ext2_iget(sb, EXT2_ROOT_INO);
    if (IS_ERR(root_inode))
        return ERR_CAST(root_inode);

    root_dentry = kzmalloc(sizeof(struct dentry));
    if (!root_dentry) {
        iput(root_inode);
        return ERR_PTR(-ENOMEM);
    }
    root_dentry->d_sb = sb;
    root_dentry->d_inode = root_inode;
    root_dentry->d_count = 1;

    sb->s_root = root_dentry;
    return root_dentry;
}
