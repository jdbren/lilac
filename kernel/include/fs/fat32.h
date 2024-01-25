#ifndef KERNEL_FAT32_H
#define KERNEL_FAT32_H

typedef struct fat_extBS_32 {
	uint32_t		FAT_size_32;
	uint16_t		extended_flags;
	uint16_t		fat_version;
	uint32_t		root_cluster;
	uint16_t		fs_info;
	uint16_t		backup_BS_sector;
	uint8_t 		reserved_0[12];
	uint8_t		    drive_number;
	uint8_t 		reserved_1;
	uint8_t		    boot_signature;
	uint32_t 		volume_id;
	unsigned char   volume_label[11];
	unsigned char   fat_type_label[8];
    uint8_t         zero[420];
    uint16_t        signature;
 
} __attribute__((packed)) fat_extBS_32_t;

typedef struct fat_BS {
    uint8_t 	jmpBoot[3];
    uint8_t 	oem_name[8];
    uint16_t	bytes_per_sector;
    uint8_t		sectors_per_cluster;
    uint16_t	reserved_sector_count;
    uint8_t	    num_FATs;
    uint16_t	root_entry_count;
    uint16_t	total_sectors_16;
    uint8_t	    media_type;
    uint16_t	FAT_size_16;
    uint16_t	sectors_per_track;
    uint16_t	num_heads;
    uint32_t	hidden_sector_count;
    uint32_t 	total_sectors_32;
 
    union {
	    fat_extBS_32_t extended_section[54];
    };
 
} __attribute__((packed)) fat_BS_t;

typedef struct {
    uint32_t fat_begin_lba;
    uint32_t cluster_begin_lba;
    uint8_t sectors_per_cluster;
    uint32_t root_dir_first_cluster;
} __attribute__((packed)) DISK;


void print_fat32_data(fat_BS_t *);

void init_fat32(DISK *disk, fat_BS_t *fat);

#endif
