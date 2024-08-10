// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _FAT32_H
#define _FAT32_H

#include <lilac/config.h>
#include <lilac/types.h>

struct super_block;
struct block_device;
struct inode;
struct dentry;
struct file;
struct dirent;

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

struct inode *fat_alloc_inode(struct super_block *sb);
void fat_destroy_inode(struct inode *inode);

struct dentry *fat32_lookup(struct inode *parent, struct dentry *find,
    unsigned int flags);


void print_fat32_data(struct fat_BS*);
struct dentry *fat32_init(struct block_device *bdev, struct super_block *sb);
int fat32_open(struct inode *inode, struct file *file);
ssize_t fat32_read(struct file *file, void *file_buf, size_t count);
ssize_t fat32_write(struct file *file, const void *file_buf, size_t count);
int fat32_readdir(struct file *file, struct dirent *dirp, unsigned int count);
int fat32_mkdir(struct inode *dir, struct dentry *new, unsigned short mode);

#endif
