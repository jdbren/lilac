#ifndef __EXT2_H__
#define __EXT2_H__

#include <lilac/endian.h>
#include <lilac/compiler.h>
#include <lilac/fs.h>
#include <lilac/errno.h>
#include <lilac/rwlock.h>
#include <drivers/blkdev.h>
#include <lib/rbtree.h>

#include "features.h"

/* data type for block offset of block group */
typedef int ext2_grpblk_t;

/* data type for filesystem-wide blocks number */
typedef unsigned long ext2_fsblk_t;

struct ext2_reserve_window {
    ext2_fsblk_t        _rsv_start;    /* First byte reserved */
    ext2_fsblk_t        _rsv_end;    /* Last byte reserved or 0 */
};

struct ext2_reserve_window_node {
    struct rb_node         rsv_node;
    u32            rsv_goal_size;
    u32            rsv_alloc_hit;
    struct ext2_reserve_window    rsv_window;
};

struct ext2_block_alloc_info {
    /* information about reservation window */
    struct ext2_reserve_window_node    rsv_window_node;
    /*
     * was i_next_alloc_block in ext2_inode_info
     * is the logical (file-relative) number of the
     * most-recently-allocated block in this file.
     * We use this for detecting linearly ascending allocation requests.
     */
    u32            last_alloc_logical_block;
    /*
     * Was i_next_alloc_goal in ext2_inode_info
     * is the *physical* companion to i_next_alloc_block.
     * it is the physical block number of the block which was most-recently
     * allocated to this file.  This gives us the goal (target) for the next
     * allocation when we detect linearly ascending requests.
     */
    ext2_fsblk_t        last_alloc_physical_block;
};

#define EXT2_NAME_LEN 255

/* reserved inode numbers */
#define EXT2_ROOT_INO           2
#define EXT2_GOOD_OLD_FIRST_INO 11

/*
 * Maximal count of links to a file
 */
#define EXT2_LINK_MAX        32000

#define EXT2_SB_MAGIC_OFFSET    0x38
#define EXT2_SB_BLOCKS_OFFSET    0x04
#define EXT2_SB_BSIZE_OFFSET    0x18

#define EXT2_SUPER_MAGIC 0xEF53
#define EXT2_DYNAMIC_REV 1

#define EXT2_FS_STATE_CLEAN  1
#define EXT2_FS_STATE_ERRORS 2

/*
 * File system states
 */
#define    EXT2_VALID_FS            0x0001    /* Unmounted cleanly */
#define    EXT2_ERROR_FS            0x0002    /* Errors detected */
#define    EFSCORRUPTED            EUCLEAN    /* Filesystem is corrupted */

/*
 * Default values for user and/or group using reserved blocks
 */
#define    EXT2_DEF_RESUID        0
#define    EXT2_DEF_RESGID        0

/*
 * Behaviour when detecting errors
 */
#define EXT2_ERRORS_CONTINUE        1    /* Continue execution */
#define EXT2_ERRORS_RO            2    /* Remount fs read-only */
#define EXT2_ERRORS_PANIC        3    /* Panic */
#define EXT2_ERRORS_DEFAULT        EXT2_ERRORS_CONTINUE

#define EXT2_OS_ID_LINUX    0
#define EXT2_OS_ID_HURD     1
#define EXT2_OS_ID_MASIX    2
#define EXT2_OS_ID_FREEBSD  3
#define EXT2_OS_ID_LITES    4

/*
 * second extended-fs super-block data in memory
 */
struct ext2_sb_info {
    unsigned long s_frag_size;    /* Size of a fragment in bytes */
    unsigned long s_frags_per_block;/* Number of fragments per block */
    unsigned long s_inodes_per_block;/* Number of inodes per block */
    unsigned long s_frags_per_group;/* Number of fragments in a group */
    unsigned long s_blocks_per_group;/* Number of blocks in a group */
    unsigned long s_inodes_per_group;/* Number of inodes in a group */
    unsigned long s_itb_per_group;    /* Number of inode table blocks per group */
    unsigned long s_gdb_count;    /* Number of group descriptor blocks */
    unsigned long s_desc_per_block;    /* Number of group descriptors per block */
    unsigned long s_groups_count;    /* Number of groups in the fs */
    // unsigned long s_overhead_last;  /* Last calculated overhead */
    // unsigned long s_blocks_last;    /* Last seen block count */
    struct blkio_desc * s_sbh;    /* Buffer containing the super block */
    struct ext2_sb * s_es;    /* Pointer to the super block in the buffer */
    struct blkio_desc ** s_group_desc;
    // unsigned long s_mount_opt;
    unsigned long s_sb_block;
    uid_t s_resuid;
    gid_t s_resgid;
    unsigned short s_mount_state;
    unsigned short s_pad;
    int s_addr_per_block_bits;
    int s_desc_per_block_bits;
    int s_inode_size;
    int s_first_ino;
    spinlock_t s_next_gen_lock;
    u32 s_next_generation;
    unsigned long s_dir_count;
    u8 *s_debts;
    // struct percpu_counter s_freeblocks_counter;
    // struct percpu_counter s_freeinodes_counter;
    // struct percpu_counter s_dirs_counter;
    // struct blockgroup_lock *s_blockgroup_lock;
    /* root of the per fs reservation window tree */
    // spinlock_t s_rsv_window_lock;
    // struct rb_root s_rsv_window_root;
    // struct ext2_reserve_window_node s_rsv_window_head;
    /*
     * s_lock protects against concurrent modifications of s_mount_state,
     * s_blocks_last, s_overhead_last and the content of superblock's
     * buffer pointed to by sbi->s_es.
     *
     * Note: It is used in ext2_show_options() to provide a consistent view
     * of the mount options.
     */
    spinlock_t s_lock;
    // struct mb_cache *s_ea_block_cache;
    // struct dax_device *s_daxdev;
};


struct ext2_sb {
    u32 s_inodes_count;
    u32 s_blocks_count;
    u32 s_r_blocks_count;
    u32 s_free_blocks_count;
    u32 s_free_inodes_count;
    u32 s_first_data_block;
    u32 s_log_block_size;
    u32 s_log_frag_size;
    u32 s_blocks_per_group;
    u32 s_frags_per_group;
    u32 s_inodes_per_group;
    u32 s_mtime;
    u32 s_wtime;
    u16 s_mnt_count;
    u16 s_max_mnt_count;
    u16 s_magic;
    u16 s_state;
    u16 s_errors;
    u16 s_minor_rev_level;
    u32 s_lastcheck;
    u32 s_checkinterval;
    u32 s_creator_os;
    u32 s_rev_level;
    u16 s_def_resuid;
    u16 s_def_resgid;

    // EXT2_DYNAMIC_REV superblock fields
    u32 s_first_ino;
    u16 s_inode_size;
    u16 s_block_group_nr;
    u32 s_feature_compat;
    u32 s_feature_incompat;
    u32 s_feature_ro_compat;
    u8  s_uuid[16];
    char s_volume_name[16];
    char s_last_mounted[64];
    u32 s_algorithm_usage_bitmap;

    // Performance hints
    u8  s_prealloc_blocks;
    u8  s_prealloc_dir_blocks;
    u16 s_padding1;

    // Journaling support
    u8  s_journal_uuid[16];
    u32 s_journal_inum;
    u32 s_journal_dev;
    u32 s_last_orphan;
    u32 s_hash_seed[4];
    u8  s_def_hash_version;
    u8  s_reserved_char_pad;
    u16 s_reserved_word_pad;
    u32 s_default_mount_opts;
    u32 s_first_meta_bg;
    u32 reserved[190];
} __packed;

#define EXT2_SB(sb) ((struct ext2_sb_info *)(sb)->s_fs_info)

#define EXT2_MIN_BLOCK_SIZE         1024
#define EXT2_MAX_BLOCK_SIZE         65536
#define EXT2_MIN_BLOCK_LOG_SIZE     10
#define EXT2_MAX_BLOCK_LOG_SIZE     16
#define EXT2_BLOCK_SIZE(s)          ((s)->s_blocksize)
#define EXT2_ADDR_PER_BLOCK(s)      (EXT2_BLOCK_SIZE(s) / sizeof (u32))
#define EXT2_BLOCK_SIZE_BITS(s)     ((s)->s_blocksize_bits)
#define EXT2_ADDR_PER_BLOCK_BITS(s) (EXT2_SB(s)->s_addr_per_block_bits)
#define EXT2_INODE_SIZE(s)          (EXT2_SB(s)->s_inode_size)
#define EXT2_FIRST_INO(s)           (EXT2_SB(s)->s_first_ino)

struct ext2_group_desc {
    u32 bg_block_bitmap;
    u32 bg_inode_bitmap;
    u32 bg_inode_table;
    u16 bg_free_blocks_count;
    u16 bg_free_inodes_count;
    u16 bg_used_dirs_count;
    u8  padding[14];
} __packed;

static_assert(sizeof(struct ext2_sb) == 1024, "ext2 superblock size incorrect");
static_assert(sizeof(struct ext2_group_desc) == 32, "ext2 block group descriptor size incorrect");

#define EXT2_BLOCKS_PER_GROUP(s)    (EXT2_SB(s)->s_blocks_per_group)
#define EXT2_DESC_PER_BLOCK(s)        (EXT2_SB(s)->s_desc_per_block)
#define EXT2_INODES_PER_GROUP(s)    (EXT2_SB(s)->s_inodes_per_group)
#define EXT2_DESC_PER_BLOCK_BITS(s)    (EXT2_SB(s)->s_desc_per_block_bits)

/*
 * Constants relative to the data blocks
 */
#define EXT2_NDIR_BLOCKS    12
#define EXT2_IND_BLOCK      EXT2_NDIR_BLOCKS
#define EXT2_DIND_BLOCK     (EXT2_IND_BLOCK + 1)
#define EXT2_TIND_BLOCK     (EXT2_DIND_BLOCK + 1)
#define EXT2_N_BLOCKS       (EXT2_TIND_BLOCK + 1)


static inline u32 ext2_block_group(u32 ino, struct super_block *sb)
{
    return (ino - 1) / EXT2_INODES_PER_GROUP(sb);
}

static inline u32 ext2_group_index(u32 ino, struct super_block *sb)
{
    return (ino - 1) % EXT2_INODES_PER_GROUP(sb);
}

static inline u32 ext2_block_num(u32 index, struct super_block *sb)
{
    return index * EXT2_INODE_SIZE(sb) / sb->s_blocksize;
}


struct ext2_inode {
    u16 i_mode;
    u16 i_uid;
    u32 i_size;
    u32 i_atime;
    u32 i_ctime;
    u32 i_mtime;
    u32 i_dtime;
    u16 i_gid;
    u16 i_nlinks;
    u32 i_disk_sectors;
    u32 i_flags;
    u32 i_osd1;
    u32 i_block[12];
    u32 i_block_indirect;
    u32 i_block_double_indirect;
    u32 i_block_triple_indirect;
    u32 i_generation;
    u32 i_file_acl;
    u32 i_dir_acl;
    u32 i_faddr;
    union {
        u8  i_osd2[12];
        struct {
            u8  i_frag;
            u8  i_fsize;
            u16 pad1;
            u16 i_uid_high;
            u16 i_gid_high;
            u32 pad2;
        } __packed linux2;
    };
} __packed;

#define i_size_high    i_dir_acl

#define i_reserved1    osd1.linux1.l_i_reserved1
#define i_frag        osd2.linux2.l_i_frag
#define i_fsize        osd2.linux2.l_i_fsize
#define i_uid_low    i_uid
#define i_gid_low    i_gid
#define i_uid_high    osd2.linux2.l_i_uid_high
#define i_gid_high    osd2.linux2.l_i_gid_high
#define i_reserved2    osd2.linux2.l_i_reserved2

#define EXT2_INODE_FLAG_SECRM     0x00000001 /* Secure deletion */
#define EXT2_INODE_FLAG_UNRM      0x00000002 /* Undeleteable */
#define EXT2_INODE_FLAG_COMPR     0x00000004 /* Compress file */
#define EXT2_INODE_FLAG_SYNC      0x00000008 /* Synchronous updates */
#define EXT2_INODE_FLAG_IMMUTABLE 0x00000010 /* Immutable file */
#define EXT2_INODE_FLAG_APPEND    0x00000020 /* writes to file may only append */
#define EXT2_INODE_FLAG_NODUMP    0x00000040 /* do not dump file */
#define EXT2_INODE_FLAG_NOATIME   0x00000080 /* do not update atime */
#define EXT2_INODE_FLAG_HASHED    0x00010000 /* hashed directory */
#define EXT2_INODE_FLAG_AFS_DIR   0x00020000 /* AFS directory */
#define EXT2_INODE_FLAG_JDATA     0x00040000 /* journal file data */

struct ext2_dir_entry {
    u32 inode;
    u16 rec_len;
    u8  name_len;
    u8  file_type;
    char name[];
};

/*
 * EXT2_DIR_PAD defines the directory entries boundaries
 *
 * NOTE: It must be a multiple of 4
 */
#define EXT2_DIR_PAD             4
#define EXT2_DIR_ROUND             (EXT2_DIR_PAD - 1)
#define EXT2_DIR_REC_LEN(name_len)    (((name_len) + 8 + EXT2_DIR_ROUND) & \
                     ~EXT2_DIR_ROUND)
#define EXT2_MAX_REC_LEN        ((1<<16)-1)

#define EXT2_DT_UNKN     0
#define EXT2_DT_REG      1
#define EXT2_DT_DIR      2
#define EXT2_DT_CHR      3
#define EXT2_DT_BLK      4
#define EXT2_DT_FIFO     5
#define EXT2_DT_SOCK     6
#define EXT2_DT_SLNK     7

#define EXT2_I(i) ((struct ext2_inode_info *)((i)->i_private))

/*
 * second extended file system inode data in memory
 */
struct ext2_inode_info {
    __le32    i_data[15];
    u32    i_flags;
    u32    i_faddr;
    u8    i_frag_no;
    u8    i_frag_size;
    u16    i_state;
    u32    i_file_acl;
    u32    i_dir_acl;
    u32    i_dtime;

    /*
     * i_block_group is the number of the block group which contains
     * this file's inode.  Constant across the lifetime of the inode,
     * it is used for making block allocation decisions - we try to
     * place a file's data blocks near its inode block, and new inodes
     * near to their parent directory's inode.
     */
    u32    i_block_group;

    /* block reservation info */
    struct ext2_block_alloc_info *i_block_alloc_info;

    u32    i_dir_start_lookup;
#ifdef CONFIG_EXT2_FS_XATTR
    /*
     * Extended attributes can be read independently of the main file
     * data. Taking i_mutex even when reading would cause contention
     * between readers of EAs and writers of regular file data, so
     * instead we synchronize on xattr_sem when reading or changing
     * EAs.
     */
    struct rw_semaphore xattr_sem;
#endif
    rwlock_t i_meta_lock;
#ifdef CONFIG_FS_DAX
    struct rw_semaphore dax_sem;
#endif

    /*
     * truncate_mutex is for serialising ext2_truncate() against
     * ext2_getblock().  It also protects the internals of the inode's
     * reservation data structures: ext2_reserve_window and
     * ext2_reserve_window_node.
     */
    struct mutex truncate_mutex;
    // struct inode    vfs_inode;
    struct list_head i_orphan;    /* unlinked but open inodes */
#ifdef CONFIG_QUOTA
    struct dquot *i_dquot[MAXQUOTAS];
#endif
};

void ext2_error(struct super_block *sb, const char *function, const char *fmt, ...);
void ext2_msg(struct super_block *sb, const char *fmt, ...);

int ext2_bg_has_super(struct super_block *sb, int group);
unsigned long ext2_bg_num_gdb(struct super_block *sb, int group);
int ext2_get_block(struct inode *inode, unsigned long iblock, u32 *bno);

int ext2_readdir(struct file *file, struct dirent *dirp, unsigned int count);
struct ext2_dir_entry *ext2_find_entry(struct inode *dir, const char *name,
        int namelen, struct blkio_desc **res_bio);
struct ext2_dir_entry *ext2_dotdot(struct inode *dir, struct blkio_desc **p);
int ext2_inode_by_name(struct inode *dir, const char *child, int namelen, ino_t *ino);

struct inode *ext2_iget(struct super_block *sb, unsigned long ino);
struct dentry *ext2_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags);

extern const struct inode_operations ext2_iops;
extern const struct file_operations ext2_dir_fops;

static inline ext2_fsblk_t
ext2_group_first_block_no(struct super_block *sb, unsigned long group_no)
{
    return group_no * (ext2_fsblk_t)EXT2_BLOCKS_PER_GROUP(sb) +
        le32_to_cpu(EXT2_SB(sb)->s_es->s_first_data_block);
}

static inline ext2_fsblk_t
ext2_group_last_block_no(struct super_block *sb, unsigned long group_no)
{
    struct ext2_sb_info *sbi = EXT2_SB(sb);

    if (group_no == sbi->s_groups_count - 1)
        return le32_to_cpu(sbi->s_es->s_blocks_count) - 1;
    else
        return ext2_group_first_block_no(sb, group_no) +
            EXT2_BLOCKS_PER_GROUP(sb) - 1;
}

#endif
