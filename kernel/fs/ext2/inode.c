#include "ext2.h"

#include <lilac/lilac.h>
#include <lilac/err.h>
#include <drivers/blkdev.h>

struct inode * ext2_alloc_inode(struct super_block *sb)
{
    struct ext2_inode_info *ei = kzmalloc(sizeof(*ei));
    struct inode *inode;

    if (!ei)
        return ERR_PTR(-ENOMEM);

    inode = kzmalloc(sizeof(*inode));
    if (!inode) {
        kfree(ei);
        return ERR_PTR(-ENOMEM);
    }

    inode->i_sb = sb;
    inode->i_private = ei;
    inode->i_count = 1;
    list_add_tail(&inode->i_list, &sb->s_inodes);

    return inode;
}

void ext2_destroy_inode(struct inode *inode)
{
    struct ext2_inode_info *ei = EXT2_I(inode);

    kfree(ei);
    kfree(inode);
}

struct ext2_group_desc * ext2_get_group_desc(struct super_block * sb,
                         unsigned int block_group,
                         struct blkio_desc ** bh)
{
    unsigned long group_desc;
    unsigned long offset;
    struct ext2_group_desc * desc;
    struct ext2_sb_info *sbi = EXT2_SB(sb);

    if (block_group >= sbi->s_groups_count) {
        ext2_error (sb, __func__,
                "block_group >= groups_count - "
                "block_group = %d, groups_count = %lu",
                block_group, sbi->s_groups_count);
        return NULL;
    }

    group_desc = block_group >> EXT2_DESC_PER_BLOCK_BITS(sb);
    offset = block_group & (EXT2_DESC_PER_BLOCK(sb) - 1);
    if (!sbi->s_group_desc[group_desc]) {
        ext2_error (sb, __func__,
                "Group descriptor not loaded - "
                "block_group = %d, group_desc = %lu, desc = %lu",
                 block_group, group_desc, offset);
        return NULL;
    }

    desc = (struct ext2_group_desc *) sbi->s_group_desc[group_desc]->b_data;
    if (bh)
        *bh = sbi->s_group_desc[group_desc];
    return desc + offset;
}

static struct ext2_inode *ext2_get_inode(struct super_block *sb, ino_t ino,
                    struct blkio_desc **p)
{
    struct blkio_desc *bh;
    unsigned long block_group;
    unsigned long block;
    unsigned long offset;
    struct ext2_group_desc *gdp;

    *p = NULL;
    if ((ino != EXT2_ROOT_INO && ino < EXT2_FIRST_INO(sb)) ||
        ino > le32_to_cpu(EXT2_SB(sb)->s_es->s_inodes_count))
        goto Einval;

    block_group = (ino - 1) / EXT2_INODES_PER_GROUP(sb);
    gdp = ext2_get_group_desc(sb, block_group, NULL);
    if (!gdp)
        goto Egdp;
    /*
     * Figure out the offset within the block group inode table
     */
    offset = ((ino - 1) % EXT2_INODES_PER_GROUP(sb)) * EXT2_INODE_SIZE(sb);
    block = le32_to_cpu(gdp->bg_inode_table) +
        (offset >> EXT2_BLOCK_SIZE_BITS(sb));
    if (!(bh = sb_bread(sb, block)))
        goto Eio;

    *p = bh;
    offset &= (EXT2_BLOCK_SIZE(sb) - 1);
    return (struct ext2_inode *) (bh->b_data + offset);

Einval:
    ext2_error(sb, __func__, "bad inode number: %lu",
           (unsigned long) ino);
    return ERR_PTR(-EINVAL);
Eio:
    ext2_error(sb, __func__,
           "unable to read inode block - inode=%lu, block=%lu",
           (unsigned long) ino, block);
Egdp:
    return ERR_PTR(-EIO);
}

/*
void ext2_set_inode_flags(struct inode *inode)
{
    unsigned int flags = EXT2_I(inode)->i_flags;

    inode->i_flags &= ~(S_SYNC | S_APPEND | S_IMMUTABLE | S_NOATIME |
                S_DIRSYNC | S_DAX);
    if (flags & EXT2_SYNC_FL)
        inode->i_flags |= S_SYNC;
    if (flags & EXT2_APPEND_FL)
        inode->i_flags |= S_APPEND;
    if (flags & EXT2_IMMUTABLE_FL)
        inode->i_flags |= S_IMMUTABLE;
    if (flags & EXT2_NOATIME_FL)
        inode->i_flags |= S_NOATIME;
    if (flags & EXT2_DIRSYNC_FL)
        inode->i_flags |= S_DIRSYNC;
    if (test_opt(inode->i_sb, DAX) && S_ISREG(inode->i_mode))
        inode->i_flags |= S_DAX;
}

void ext2_set_file_ops(struct inode *inode)
{
    inode->i_op = &ext2_file_inode_operations;
    inode->i_fop = &ext2_file_operations;
    if (IS_DAX(inode))
        inode->i_mapping->a_ops = &ext2_dax_aops;
    else
        inode->i_mapping->a_ops = &ext2_aops;
}
*/

struct inode *ext2_iget(struct super_block *sb, unsigned long ino)
{
    struct ext2_inode_info *ei;
    struct blkio_desc *bh = NULL;
    struct ext2_inode *raw_inode;
    struct inode *inode;
    long ret = -EIO;
    int n;
    uid_t i_uid;
    gid_t i_gid;

    // inode = iget_locked(sb, ino);
    inode = alloc_inode(sb);
    if (!inode)
        return ERR_PTR(-ENOMEM);
    // if (!(inode->i_state & I_NEW))
    //     return inode;

    ei = EXT2_I(inode);
    ei->i_block_alloc_info = NULL;

    raw_inode = ext2_get_inode(inode->i_sb, ino, &bh);
    if (IS_ERR(raw_inode)) {
        ret = PTR_ERR(raw_inode);
        goto bad_inode;
    }

    inode->i_mode = le16_to_cpu(raw_inode->i_mode);
    i_uid = (uid_t)le16_to_cpu(raw_inode->i_uid_low);
    i_gid = (gid_t)le16_to_cpu(raw_inode->i_gid_low);
    /* if (!(test_opt (inode->i_sb, NO_UID32))) {
        i_uid |= le16_to_cpu(raw_inode->i_uid_high) << 16;
        i_gid |= le16_to_cpu(raw_inode->i_gid_high) << 16;
    } */
    inode->i_uid = i_uid;
    inode->i_gid = i_gid;
    inode->i_nlink = le16_to_cpu(raw_inode->i_nlinks);
    inode->i_size = le32_to_cpu(raw_inode->i_size);
    inode->i_atime = le32_to_cpu(raw_inode->i_atime);
    inode->i_ctime = le32_to_cpu(raw_inode->i_ctime);
    inode->i_mtime = le32_to_cpu(raw_inode->i_mtime);
    ei->i_dtime = le32_to_cpu(raw_inode->i_dtime);
    /* We now have enough fields to check if the inode was active or not.
     * This is needed because nfsd might try to access dead inodes
     * the test is that same one that e2fsck uses
     * NeilBrown 1999oct15
     */
    if (inode->i_nlink == 0 && (inode->i_mode == 0 || ei->i_dtime)) {
        /* this inode is deleted */
        ret = -ESTALE;
        goto bad_inode;
    }
    inode->i_blocks = le32_to_cpu(raw_inode->i_blocks);
    ei->i_flags = le32_to_cpu(raw_inode->i_flags);
    // ext2_set_inode_flags(inode);
    ei->i_faddr = le32_to_cpu(raw_inode->i_faddr);
    // ei->i_frag_no = raw_inode->i_frag;
    // ei->i_frag_size = raw_inode->i_fsize;
    ei->i_file_acl = le32_to_cpu(raw_inode->i_file_acl);
    ei->i_dir_acl = 0;

    /* if (ei->i_file_acl &&
        !ext2_data_block_valid(EXT2_SB(sb), ei->i_file_acl, 1)) {
        ext2_error(sb, "ext2_iget", "bad extended attribute block %u",
               ei->i_file_acl);
        ret = -EFSCORRUPTED;
        goto bad_inode;
    } */

    if (S_ISREG(inode->i_mode))
        inode->i_size |= ((u64)le32_to_cpu(raw_inode->i_size_high)) << 32;
    else
        ei->i_dir_acl = le32_to_cpu(raw_inode->i_dir_acl);
    if (inode->i_size < 0) {
        ret = -EFSCORRUPTED;
        goto bad_inode;
    }
    ei->i_dtime = 0;
    // inode->i_generation = le32_to_cpu(raw_inode->i_generation);
    ei->i_state = 0;
    ei->i_block_group = (ino - 1) / EXT2_INODES_PER_GROUP(inode->i_sb);
    ei->i_dir_start_lookup = 0;

    /*
     * NOTE! The in-memory inode i_data array is in little-endian order
     * even on big-endian machines: we do NOT byteswap the block numbers!
     */
    for (n = 0; n < EXT2_N_BLOCKS; n++)
        ei->i_data[n] = raw_inode->i_block[n];

    if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &ext2_dir_iops;
        inode->i_fop = &ext2_dir_fops;
    } else {
        inode->i_op = &ext2_file_iops;
        inode->i_fop = &ext2_file_fops;
    }

    bdrop(bh);
    // unlock_new_inode(inode);
    return inode;

bad_inode:
    bdrop(bh);
    destroy_inode(inode);
    return ERR_PTR(ret);
}

struct dentry * ext2_lookup(struct inode *dir, struct dentry *dentry,
    unsigned int flags)
{
    struct inode *inode;
    ino_t ino;
    int res;

    if (dentry->d_name.len > EXT2_NAME_LEN)
        return ERR_PTR(-ENAMETOOLONG);

    res = ext2_inode_by_name(dir, dentry->d_name.data, dentry->d_name.len, &ino);
    if (res) {
        if (res != -ENOENT)
            return ERR_PTR(res);
        return NULL;
    }

    inode = ext2_iget(dir->i_sb, ino);
    if (IS_ERR(inode))
        return ERR_CAST(inode);

    dentry->d_inode = inode;
    return NULL;
}
