#ifndef _FAT32_H
#define _FAT32_H

#include <kernel/types.h>

typedef struct fat_extBS_32 {
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
 
} __attribute__((packed)) fat_extBS_32_t;

typedef struct fat_BS {
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
	    fat_extBS_32_t extended_section;
    };
 
} __attribute__((packed)) fat_BS_t;

typedef struct disk {
    u32 fat_begin_lba;
    u32 cluster_begin_lba;
    u8 sectors_per_cluster;
    u32 root_dir_first_cluster;
} __attribute__((packed)) DISK;


void print_fat32_data(fat_BS_t *);
void fat32_init(int disknum, u32 boot_sector);

#endif
