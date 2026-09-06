#include "ext2.h"

#include <lilac/err.h>
#include <mm/kmalloc.h>

const char * simple_get_link(struct dentry *dentry, struct inode *inode)
{
    return inode->i_link;
}

const char * ext2_get_link(struct dentry *dentry, struct inode *inode)
{
    struct ext2_inode_info *ext2_inode = EXT2_I(inode);
    if (inode->i_link)
        return inode->i_link;

    if (ext2_inode_is_fast_symlink(inode)) {
        return (char*)ext2_inode->i_data;
    } else {
        struct blkio_desc *bh = sb_bread(inode->i_sb, le32_to_cpu(ext2_inode->i_data[0]));
        if (IS_ERR_OR_NULL(bh))
            return bh ? ERR_CAST(bh) : ERR_PTR(-EIO);
        nd_terminate_link(bh->b_data, inode->i_size, inode->i_sb->s_blocksize - 1);
        char *link = strdup((const char*)bh->b_data);
        if (!link) {
            bdrop(bh);
            return ERR_PTR(-ENOMEM);
        }
        bdrop(bh);
        inode->i_link = link;
        return inode->i_link;
    }
}

const struct inode_operations ext2_symlink_iops = {
    .get_link = ext2_get_link,
};

const struct inode_operations ext2_fast_symlink_iops = {
    .get_link = simple_get_link,
};
