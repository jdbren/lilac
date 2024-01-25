#include <stdio.h>
#include <stdint.h>
#include <fs/fat32.h>

void init_fat32(DISK *disk, fat_BS_t *fat) {
    disk->fat_begin_lba = fat->hidden_sector_count + fat->reserved_sector_count;
    disk->cluster_begin_lba = fat->hidden_sector_count + fat->reserved_sector_count
        + (fat->num_FATs * fat->extended_section->FAT_size_32);
    disk->sectors_per_cluster = fat->sectors_per_cluster;
    disk->root_dir_first_cluster = fat->extended_section->root_cluster;
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
    printf("Root cluster: %x\n", ptr->extended_section->root_cluster);
    printf("Drive number: %x\n", ptr->extended_section->drive_number);
}
