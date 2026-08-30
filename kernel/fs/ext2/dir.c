#include "ext2.h"

#include <lilac/lilac.h>
#include <lilac/err.h>
#include <drivers/blkdev.h>

typedef struct ext2_dir_entry ext2_dirent;

/*
 * Tests against MAX_REC_LEN etc were put in place for 64k block
 * sizes; if that is not possible on this arch, we can skip
 * those tests and speed things up.
 */
static inline unsigned ext2_rec_len_from_disk(__le16 dlen)
{
    return le16_to_cpu(dlen);
}

static inline __le16 ext2_rec_len_to_disk(unsigned len)
{
    return cpu_to_le16(len);
}

/*
 * ext2 uses block-sized chunks. Arguably, sector-sized ones would be
 * more robust, but we have what we have
 */
static inline unsigned ext2_chunk_size(struct inode *inode)
{
    return inode->i_sb->s_blocksize;
}

/* number of blocks spanned by the directory (formerly page-cache "pages") */
static inline unsigned long ext2_dir_pages(struct inode *inode)
{
    unsigned long blocksize = inode->i_sb->s_blocksize;
    return (inode->i_size + blocksize - 1) / blocksize;
}

/* number of valid bytes within the nth directory block */
static inline unsigned ext2_last_byte(struct inode *inode, unsigned long n)
{
    unsigned long blocksize = inode->i_sb->s_blocksize;
    u64 last_byte = inode->i_size - (u64)n * blocksize;

    if (last_byte > blocksize)
        last_byte = blocksize;
    return (unsigned)last_byte;
}

/* read-only fetch of the nth block of a directory's data */
static struct blkio_desc *ext2_get_dir_block(struct inode *dir, unsigned long block)
{
    u32 bno;
    int err;
    struct blkio_desc *bio;

    err = ext2_get_block(dir, block, &bno);
    if (err)
        return ERR_PTR(err);
    if (!bno)
        return ERR_PTR(-EIO); /* hole in a directory is corruption */

    bio = sb_bread(dir->i_sb, bno);
    if (IS_ERR_OR_NULL(bio))
        return bio ? bio : ERR_PTR(-EIO);
    return bio;
}

/*
 * NOTE! unlike strncmp, ext2_match returns 1 for success, 0 for failure.
 *
 * len <= EXT2_NAME_LEN and de != NULL are guaranteed by caller.
 */
static inline int ext2_match (int len, const char * const name,
                    struct ext2_dir_entry * de)
{
    if (len != de->name_len)
        return 0;
    if (!de->inode)
        return 0;
    return !memcmp(name, de->name, len);
}

/*
 * p is at least 6 bytes before the end of block
 */
static inline ext2_dirent *ext2_next_entry(ext2_dirent *p)
{
    return (ext2_dirent *)((char *)p +
            ext2_rec_len_from_disk(p->rec_len));
}

static inline void ext2_set_de_type(ext2_dirent *de, struct inode *inode)
{
    if (EXT2_HAS_INCOMPAT_FEATURE(inode->i_sb, EXT2_FEATURE_INCOMPAT_FILETYPE))
        de->file_type = fs_umode_to_ftype(inode->i_mode);
    else
        de->file_type = 0;
}

int ext2_readdir(struct file *file, struct dirent *dirp, unsigned int count)
{
    struct inode *inode = file->f_dentry->d_inode;
    struct super_block *sb = inode->i_sb;
    unsigned long blocksize = sb->s_blocksize;
    unsigned long npages = ext2_dir_pages(inode);
    unsigned long n = file->f_pos / blocksize;
    unsigned int offset = file->f_pos % blocksize;
    bool has_filetype = EXT2_HAS_INCOMPAT_FEATURE(sb, EXT2_FEATURE_INCOMPAT_FILETYPE);
    unsigned int filled = 0;

    if (file->f_pos > inode->i_size - EXT2_DIR_REC_LEN(1))
        return 0;

    for ( ; n < npages && filled < count; n++, offset = 0) {
        char *kaddr, *limit;
        ext2_dirent *de;
        struct blkio_desc *bio = ext2_get_dir_block(inode, n);

        if (IS_ERR(bio)) {
            ext2_error(sb, __func__, "bad block in dir #%lu", inode->i_ino);
            return filled ? (int)filled : (int)PTR_ERR(bio);
        }

        kaddr = (char *)bio->b_data;
        de = (ext2_dirent *)(kaddr + offset);
        limit = kaddr + ext2_last_byte(inode, n) - EXT2_DIR_REC_LEN(1);
        for ( ; (char *)de <= limit && filled < count; de = ext2_next_entry(de)) {
            if (de->rec_len == 0) {
                ext2_error(sb, __func__,
                    "zero-length directory entry");
                brelease(bio);
                return filled ? (int)filled : -EIO;
            }
            if (de->inode) {
                struct dirent *out = &dirp[filled];
                unsigned int namelen = MIN((unsigned int)de->name_len,
                        sizeof(out->d_name) - 1);

                memcpy(out->d_name, de->name, namelen);
                out->d_name[namelen] = '\0';
                out->d_ino = le32_to_cpu(de->inode);
                out->d_reclen = sizeof(struct dirent);
                out->d_type = has_filetype ?
                    fs_ftype_to_dtype(de->file_type) : DT_UNKNOWN;
                filled++;
            }
            file->f_pos += ext2_rec_len_from_disk(de->rec_len);
        }
        brelease(bio);
    }

    return (int)filled;
}

/*
 *	ext2_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the block in which the entry was found (as a parameter -
 * res_bio), and the entry itself. Entry is guaranteed to be valid.
 */
struct ext2_dir_entry *ext2_find_entry (struct inode *dir,
            const char *name, int namelen, struct blkio_desc **res_bio)
{
    unsigned reclen = EXT2_DIR_REC_LEN(namelen);
    unsigned long start, n;
    unsigned long npages = ext2_dir_pages(dir);
    struct blkio_desc *bio = NULL;
    struct ext2_inode_info *ei = EXT2_I(dir);
    ext2_dirent * de;

    if (npages == 0)
        goto out;

    *res_bio = NULL;

    start = ei->i_dir_start_lookup;
    if (start >= npages)
        start = 0;
    n = start;
    do {
        char *kaddr;
        bio = ext2_get_dir_block(dir, n);
        if (IS_ERR(bio))
            return ERR_CAST(bio);

        kaddr = (char *)bio->b_data;
        de = (ext2_dirent *) kaddr;
        kaddr += ext2_last_byte(dir, n) - reclen;
        while ((char *) de <= kaddr) {
            if (de->rec_len == 0) {
                ext2_error(dir->i_sb, __func__,
                    "zero-length directory entry");
                brelease(bio);
                goto out;
            }
            if (ext2_match(namelen, name, de))
                goto found;
            de = ext2_next_entry(de);
        }
        brelease(bio);

        if (++n >= npages)
            n = 0;
        /* next block is past the blocks we've got */
        if (unlikely(n > (dir->i_blocks * 512) / dir->i_sb->s_blocksize)) {
            ext2_error(dir->i_sb, __func__,
                "dir %lu size %lld exceeds block count %u",
                dir->i_ino, dir->i_size,
                (unsigned int)dir->i_blocks);
            goto out;
        }
    } while (n != start);
out:
    return ERR_PTR(-ENOENT);

found:
    *res_bio = bio;
    ei->i_dir_start_lookup = n;
    return de;
}

struct ext2_dir_entry * ext2_dotdot (struct inode *dir, struct blkio_desc **p)
{
    struct blkio_desc *bio = ext2_get_dir_block(dir, 0);
    ext2_dirent *de = NULL;

    if (!IS_ERR(bio)) {
        de = ext2_next_entry((ext2_dirent *) bio->b_data);
        *p = bio;
    }
    return de;
}

int ext2_inode_by_name(struct inode *dir, const char *child, int namelen, ino_t *ino)
{
    struct ext2_dir_entry *de;
    struct blkio_desc *bio;

    de = ext2_find_entry(dir, child, namelen, &bio);
    if (IS_ERR(de))
        return PTR_ERR(de);

    *ino = le32_to_cpu(de->inode);
    bdrop(bio);
    return 0;
}

const struct file_operations ext2_fops = {
    .readdir = ext2_readdir,
};
