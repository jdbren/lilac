#include <string.h>
#include <kernel/types.h>
#include <fs/fat32.h>
#include <mm/kheap.h>
#include <asm/diskio.h>

#define LONG_FNAME 0x0F
#define UNUSED 0xE5
#define MAX_SECTOR_READS 256
#define FAT_BUFFER_SIZE 512 * MAX_SECTOR_READS

u8 fat_buffer[FAT_BUFFER_SIZE];

static inline u32 LBA_ADDR(u32 cluster_num, DISK *disk) {
    return disk->cluster_begin_lba + 
    ((cluster_num - disk->root_dir_first_cluster) * disk->sectors_per_cluster);
}

typedef struct fat_dir {
    char name[8];
    char ext[3];
    u8 attributes;
    u8 reserved;
    u8 creation_time_tenths;
    u16 creation_time;
    u16 creation_date;
    u16 last_access_date;
    u16 first_cluster_high;
    u16 last_write_time;
    u16 last_write_date;
    u16 first_cluster_low;
    u32 file_size;
} __attribute__((packed)) fat_dir_t;

DISK disks[4];

void init_fat32(int disknum, fat_BS_t *fat) {
    DISK *disk = &disks[disknum];
    disk->fat_begin_lba = fat->hidden_sector_count + fat->reserved_sector_count;
    disk->cluster_begin_lba = fat->hidden_sector_count + fat->reserved_sector_count
        + (fat->num_FATs * fat->extended_section->FAT_size_32);
    disk->sectors_per_cluster = fat->sectors_per_cluster;
    disk->root_dir_first_cluster = fat->extended_section->root_cluster;

    printf("fat_begin_lba: %x\n", disk->fat_begin_lba);
    printf("cluster_begin_lba: %x\n", disk->cluster_begin_lba);
    printf("sectors_per_cluster: %x\n", disk->sectors_per_cluster);
    printf("root_dir_first_cluster: %x\n", disk->root_dir_first_cluster);

    u8 *buffer = kmalloc(512);
    printf("Allocated %x\n", buffer);
    printf("LBA %x\n", LBA_ADDR(disk->root_dir_first_cluster, disk));
    disk_read(LBA_ADDR(disk->root_dir_first_cluster, disk), 1, (u32)buffer);
    fat_dir_t *root_dir = (fat_dir_t*)buffer;

    for (int i = 0; root_dir[i].name[0] != 0; i++) {
        if (root_dir[i].attributes != LONG_FNAME && root_dir[i].name[0] != (char)UNUSED) {
            printf("Name: ");
            for(int j = 0; j < 8; j++)
                printf("%c", root_dir[i].name[j]);
            printf("\n");
            printf("index: %d\n", i);
        }
    }

    disk_read(disk->fat_begin_lba, MAX_SECTOR_READS, (u32)fat_buffer);
    // printf("FAT[2]: %x\n", *(u32*)&fat_buffer[8]);

    fat_dir_t *subdir_buffer = kmalloc(512);
    printf("Allocated %x\n", subdir_buffer);
    
    printf("LBA_ADDR: %x\n", LBA_ADDR(root_dir[3].first_cluster_low + root_dir[3].first_cluster_high, disk));
    disk_read(LBA_ADDR(root_dir[3].first_cluster_low + root_dir[3].first_cluster_high, disk), 1, (u32)subdir_buffer);
    //asm volatile("hlt");
    for (int i = 0; subdir_buffer[i].name[0] != 0; i++) {
        if (subdir_buffer[i].attributes != LONG_FNAME && subdir_buffer[i].name[0] != (char)UNUSED) {
            printf("Name: ");
            for(int j = 0; j < 8; j++)
                printf("%c", subdir_buffer[i].name[j]);
            printf("\n");
        }
    }

    // unsigned char FAT_table[fat->bytes_per_sector];
    // unsigned int fat_offset = active_cluster * 4;
    // unsigned int fat_sector = first_fat_sector + (fat_offset / fat->bytes_per_sector);
    // unsigned int ent_offset = fat_offset % fat->bytes_per_sector;
    
    // //at this point you need to read from sector "fat_sector" on the disk into "FAT_table".
    
    // //remember to ignore the high 4 bits.
    // unsigned int table_value = *(unsigned int*)&FAT_table[ent_offset];
    // if (fat32) table_value &= 0x0FFFFFFF;
 
//the variable "table_value" now has the information you need about the next cluster in the chain.
}

void print_fat32_data(fat_BS_t *ptr) {
    printf("FAT32 data:\n");
    printf("Bytes per sector: %x\n", ptr->bytes_per_sector);
    printf("Sectors per cluster: %x\n", ptr->sectors_per_cluster);
    printf("Reserved sector count: %x\n", ptr->reserved_sector_count);
    printf("Table count: %x\n", ptr->num_FATs);
    printf("Root entry count: %x\n", ptr->root_entry_count);
    printf("Media type: %x\n", ptr->media_type);
    printf("Hidden sector count: %x\n", ptr->hidden_sector_count);
    printf("Total sectors 32: %x\n", ptr->total_sectors_32);

    printf("FAT32 extended data:\n");
    printf("FAT size 32: %x\n", ptr->extended_section->FAT_size_32);
}
