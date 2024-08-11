#ifndef _FAT_INTERNAL_H
#define _FAT_INTERNAL_H

#include <lilac/types.h>
#include <lilac/config.h>

struct fat_extBS_32 {
	u32		FAT_size_32;
	u16		extended_flags;
	u16		fat_version;
	u32		root_cluster;
	u16		fs_info;
	u16		backup_BS_sector;
	u8 		reserved_0[12];
	u8		drive_number;
	u8 		reserved_1;
	u8		boot_signature;
	u32     volume_id;
	char    volume_label[11];
	char    fat_type_label[8];
    u8      zero[420];
    u16     signature;
} __packed;

struct fat_BS {
    u8 	jmpBoot[3];
    u8 	oem_name[8];
    u16	bytes_per_sector;
    u8	sectors_per_cluster;
    u16	reserved_sector_count;
    u8	num_FATs;
    u16	root_entry_count;
    u16	total_sectors_16;
    u8	media_type;
    u16	FAT_size_16;
    u16	sectors_per_track;
    u16	num_heads;
    u32	hidden_sector_count;
    u32 total_sectors_32;

    union {
	    struct fat_extBS_32 extended_section;
    };
} __packed;

struct FSInfo {
    u32 lead_sig;
    u8 res[480];
    u32 struct_sig;
    u32 free_clst_cnt;
    u32 next_free_clst;
    u8 res2[12];
    u32 trail_sig;
} __packed;

struct fat_file {
    char name[8];
    char ext[3];
    u8 attributes;
    u8 reserved;
    u8 creation_time_tenths;
    u16 creation_time;
    u16 creation_date;
    u16 last_access_date;
    u16 cl_high;
    u16 last_write_time;
    u16 last_write_date;
    u16 cl_low;
    u32 file_size;
} __packed;

#define LONG_FNAME 0x0F
#define FAT_DIR_ATTR 0x10
#define UNUSED 0xE5
#define MAX_SECTOR_READS 64
#define BYTES_PER_SECTOR 512
#define FAT_BUFFER_SIZE (BYTES_PER_SECTOR * MAX_SECTOR_READS)

#define ROUND_UP(x,bps)    ((((uintptr_t)(x)) + (u32)bps-1) & (~((u32)bps-1)))

#define FAT_SIGNATURE 0xAA55
#define FAT32_FS_INFO_SIG1 0x41615252
#define FAT32_FS_INFO_SIG2 0x61417272
#define FAT32_FS_INFO_TRAIL_SIG 0xAA550000

#define FAT_SET_DATE(year, month, day) \
    (((year - 1980) << 9) | (month << 5) | day)
#define FAT_SET_TIME(hour, min, sec) \
    ((hour << 11) | (min << 5) | (sec >> 1))


struct fat_FAT_buf {
    u32 first_clst;
    u32 last_clst;
    u32 sectors;
    volatile u32 buf[FAT_BUFFER_SIZE / 4];
};

struct fat_file_buf {
    u32 cl;
    u32 buf_sz;
    volatile u8 *buffer;
};

struct fat_disk {
    u32 base_lba;
    u32 fat_begin_lba;
    u32 clst_begin_lba;
    u32 root_start;
    u16 bytes_per_clst;
    u16 sect_per_clst;
    volatile struct fat_BS bpb;
    volatile struct FSInfo fs_info;
    struct fat_FAT_buf FAT;
};

struct super_block;
struct inode;
struct file;
struct gendisk;

extern const struct file_operations fat_fops;
extern const struct super_operations fat_sops;
extern const struct inode_operations fat_iops;

void print_fat32_data(struct fat_BS*);
extern inline struct inode *fat_alloc_inode(struct super_block *sb);
extern inline void fat_destroy_inode(struct inode *inode);
struct inode *fat_build_inode(struct super_block *sb, struct fat_file *info);

extern inline void __fat_read_clst(struct fat_disk *fat_disk, struct gendisk *hd, u32 clst, void *buf);
extern inline void __fat_write_clst(struct fat_disk *fat_disk, struct gendisk *hd, u32 clst, const void *buf);
extern inline u32 __get_FAT_val(u32 clst, struct fat_disk *disk);
u32 __fat_get_clst_num(struct file *file, struct fat_disk *disk);
u32 __fat_find_free_clst(struct fat_disk *disk);

int __must_check fat32_write_fs_info(struct fat_disk *fat_disk, struct gendisk *gd);
int fat_write_FAT(struct fat_disk *fat_disk, struct gendisk *gd);

int __do_fat32_read(const struct file *file, u32 clst, volatile u8 *buffer, size_t num_clst);
int __do_fat32_write(const struct file *file, u32 clst, const u8 *buffer, size_t num_clst);

#endif