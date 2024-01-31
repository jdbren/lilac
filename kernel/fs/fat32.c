#include <string.h>
#include <kernel/types.h>
#include <fs/fat32.h>
#include <mm/kheap.h>
#include <asm/diskio.h>

#define LONG_FNAME 0x0F
#define UNUSED 0xE5
#define MAX_SECTOR_READS 256
#define FAT_BUFFER_SIZE 512 * MAX_SECTOR_READS

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
fat_BS_t volID;
u8 fat_buffer[FAT_BUFFER_SIZE];

static inline u32 LBA_ADDR(u32 cluster_num, DISK *disk)
{
    return disk->cluster_begin_lba + 
        ((cluster_num - disk->root_dir_first_cluster) 
        * disk->sectors_per_cluster);
}

void disk_init(DISK *disk, fat_BS_t *id)
{
    disk->fat_begin_lba = id->hidden_sector_count + id->reserved_sector_count;
    disk->cluster_begin_lba = id->hidden_sector_count + id->reserved_sector_count
        + (id->num_FATs * id->extended_section.FAT_size_32);
    disk->sectors_per_cluster = id->sectors_per_cluster;
    disk->root_dir_first_cluster = id->extended_section.root_cluster;
}


void fat32_init(int disknum, u32 boot_sector) {
    disk_read(boot_sector, 1, (u32)&volID);
    DISK *disk = &disks[disknum];
    disk_init(disk, &volID);

    u8 *buffer = kmalloc(512);
    disk_read(LBA_ADDR(disk->root_dir_first_cluster, disk), 1, (u32)buffer);
    fat_dir_t *root_dir = (fat_dir_t*)buffer;

    for (int i = 0; root_dir[i].name[0] != 0; i++) {
        if (root_dir[i].attributes != LONG_FNAME && root_dir[i].name[0] != (char)UNUSED) {
            printf("Name: ");
            const char *str = "BIN     ";
            if (!memcmp(root_dir[i].name, str, 8))
                for (int j = 0; j < 8; j++)
                    printf("%c", root_dir[i].name[j]);
            printf("\n");
            printf("index: %d\n", i);
        }
    }

    disk_read(disk->fat_begin_lba, MAX_SECTOR_READS, (u32)fat_buffer);

    fat_dir_t *subdir_buffer = kmalloc(512);
    disk_read(LBA_ADDR(root_dir[5].first_cluster_low + root_dir[5].first_cluster_high, disk), 1, (u32)subdir_buffer);

    for (int i = 0; subdir_buffer[i].name[0] != 0; i++) {
        if (subdir_buffer[i].attributes != LONG_FNAME && subdir_buffer[i].name[0] != (char)UNUSED) {
            printf("Name: ");
            for(int j = 0; j < 8; j++)
                printf("%c", subdir_buffer[i].name[j]);
            printf("\n");
        }
    }

    // unsigned char FAT_table[volID->bytes_per_sector];
    // unsigned int fat_offset = active_cluster * 4;
    // unsigned int fat_sector = first_fat_sector + (fat_offset / volID->bytes_per_sector);
    // unsigned int ent_offset = fat_offset % volID->bytes_per_sector;
    
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
    printf("FAT size 32: %x\n", ptr->extended_section.FAT_size_32);
}
