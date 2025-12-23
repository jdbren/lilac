#ifndef __EXT2_H__
#define __EXT2_H__

#include <lilac/endian.h>
#include <lilac/compiler.h>
#include <lilac/fs.h>

#define EXT2_SUPER_MAGIC 0xEF53
#define EXT2_DYNAMIC_REV 1

#define EXT2_FS_STATE_CLEAN  1
#define EXT2_FS_STATE_ERRORS 2

#define EXT2_ERRORS_CONTINUE 1
#define EXT2_ERRORS_RO       2
#define EXT2_ERRORS_PANIC    3

#define EXT2_OS_ID_LINUX    0
#define EXT2_OS_ID_HURD     1
#define EXT2_OS_ID_MASIX    2
#define EXT2_OS_ID_FREEBSD  3
#define EXT2_OS_ID_LITES    4

#define EXT2_FEATURE_PREALLOC   0x0001
#define EXT2_FEATURE_AFS_SERVER 0x0002
#define EXT2_FEATURE_JOURNAL    0x0004
#define EXT2_FEATURE_EXT_ATTR   0x0008
#define EXT2_FEATURE_RESIZE_INO 0x0010
#define EXT2_FEATURE_DIR_HASH   0x0020

#define EXT2_FEATURE_COMPRESSION    0x0001
#define EXT2_FEATURE_FILETYPE       0x0002
#define EXT2_FEATURE_RECOVER        0x0004
#define EXT2_FEATURE_JOURNAL_DEV    0x0008

#define EXT2_FEATURE_RO_SPARSE_SUPER 0x0001
#define EXT2_FEATURE_RO_LARGE_FILE   0x0002
#define EXT2_FEATURE_RO_BTREE_DIR    0x0004

struct ext2_sb_mem {
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
} __packed;

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

#define EXT2_SB(sb) ((struct ext2_sb_mem *)(sb)->s_fs_info)

#define EXT2_BLOCKS_PER_GROUP(s)	(EXT2_SB(s)->s_blocks_per_group)
#define EXT2_DESC_PER_BLOCK(s)		(EXT2_SB(s)->s_desc_per_block)
#define EXT2_INODES_PER_GROUP(s)	(EXT2_SB(s)->s_inodes_per_group)
#define EXT2_DESC_PER_BLOCK_BITS(s)	(EXT2_SB(s)->s_desc_per_block_bits)
#define EXT2_INODE_SIZE(sb)		    (EXT2_SB(sb)->s_inode_size ? EXT2_SB(sb)->s_inode_size : 128)

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
} __packed;

#define EXT2_DT_UNKN     0
#define EXT2_DT_REG      1
#define EXT2_DT_DIR      2
#define EXT2_DT_CHR      3
#define EXT2_DT_BLK      4
#define EXT2_DT_FIFO     5
#define EXT2_DT_SOCK     6
#define EXT2_DT_SLNK     7


struct super_block *ext2_read_super(struct block_device *bdev, struct super_block *sb);

#endif
