#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <kernel/types.h>
#include <kernel/panic.h>
#include <fs/fat32.h>
#include <mm/kheap.h>
#include <asm/diskio.h>

#define LONG_FNAME 0x0F
#define DIR 0x10
#define UNUSED 0xE5
#define MAX_SECTOR_READS 256
#define FAT_BUFFER_SIZE 512 * MAX_SECTOR_READS

typedef struct fat_file {
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
} __attribute__((packed)) fat_file_t;


static DISK disks[4];
static fat_BS_t volID;
static u32 fat_buffer[FAT_BUFFER_SIZE / 4];

static inline u32 LBA_ADDR(u32 cluster_num, DISK *disk)
{
    return disk->cluster_begin_lba + 
        ((cluster_num - disk->root_start) * disk->sectors_per_cluster);
}

void disk_init(DISK *disk, fat_BS_t *id)
{
    disk->fat_begin_lba = id->hidden_sector_count + id->reserved_sector_count;
    disk->cluster_begin_lba = id->hidden_sector_count + id->reserved_sector_count
        + (id->num_FATs * id->extended_section.FAT_size_32);
    disk->sectors_per_cluster = id->sectors_per_cluster;
    disk->root_start = id->extended_section.root_cluster;
}


static inline int name_length(const char* name, char delim)
{
    int i = 0;
    while (name[i] != delim && name[i] != 0)
        i++;
    
    return i;
}

void fat32_init(int disknum, u32 boot_sector)
{
    disk_read(boot_sector, 1, (u32)&volID);
    DISK *disk = &disks[disknum];
    disk_init(disk, &volID);

    // u8 *buffer = kmalloc(512);
    // disk_read(LBA_ADDR(disk->root_start, disk), 1, (u32)buffer);
    // fat_file_t *root_dir = (fat_file_t*)buffer;

    // for (int i = 0; root_dir[i].name[0] != 0; i++) {
    //     if (root_dir[i].attributes != LONG_FNAME && root_dir[i].name[0] != (char)UNUSED) {
    //         printf("Name: ");
    //         const char *str = "BIN     ";
    //         if (!memcmp(root_dir[i].name, str, 8))
    //             for (int j = 0; j < 8; j++)
    //                 printf("%c", root_dir[i].name[j]);
    //         printf("\n");
    //         printf("index: %d\n", i);
    //     }
    // }

    disk_read(disk->fat_begin_lba, MAX_SECTOR_READS, (u32)fat_buffer);

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

bool check_entry(fat_file_t *entry, DISK *disk, const char *cur, 
              int dname_len)
{
    if (entry->attributes != LONG_FNAME && entry->name[0] != (char)UNUSED) { 
        if (!memcmp(entry->name, cur, dname_len)) {
            printf("Found directory or file: %s\n", cur);
            return true;
        }
    }
    return false;
}

u32 check_dir(fat_file_t **entry, void *sec_buf, DISK *disk, const char *cur, 
              int dname_len)
{
    for (; *entry < sec_buf + 512 && (*entry)->name[0] != 0; (*entry)++) {
        if(check_entry(*entry, disk, cur, dname_len))
            return (*entry)->cl_low + (*entry)->cl_high;
    }
    return 0;
}

void* fat32_read_file(const char *path)
{
    void *file_buf = NULL;
    DISK *disk = &disks[0];
    u8 *sec_buf = kmalloc(disk->sectors_per_cluster * 512);
    fat_file_t *entry = NULL;
    
    char cur[9];
    int dname_len;

    dname_len = name_length(++path, '/');
    for (int i = 0; i < dname_len; i++)
        cur[i] = toupper(path[i]);
    cur[dname_len] = 0;
    printf("Looking for: %s\n", cur);

    disk_read(LBA_ADDR(disk->root_start, disk), disk->sectors_per_cluster, 
            (u32)sec_buf);
    entry = (fat_file_t*)sec_buf;

    while (1) {
        u32 clst = check_dir(&entry, sec_buf, disk, cur, dname_len);
        if (clst == 0) {
            printf("File not found\n");
            goto cleanup;
        } 
        else if (entry->attributes == DIR) {
            disk_read(LBA_ADDR(clst, disk), disk->sectors_per_cluster, (u32)sec_buf);
            entry = (fat_file_t*)sec_buf;
            path += dname_len + 1;
            dname_len = name_length(path, '/');
            for (int i = 0; i < dname_len; i++)
                cur[i] = toupper(path[i]);
            cur[dname_len] = 0;
        }
        else break;
    }

    printf("Found file: %s\n", entry->name);
    printf("File size: %d\n", entry->file_size);

    file_buf = kmalloc(entry->file_size);
    u32 clst = (u32)entry->cl_low + (u32)entry->cl_high;
    u32 fat_value = 0;
    void *tmp = file_buf;
    while (fat_value < 0x0FFFFFF8) {
        disk_read(LBA_ADDR(clst, disk), disk->sectors_per_cluster, (u32)tmp);
        if(clst >= 32768)
            kerror("clst out of bounds\n");
        fat_value = fat_buffer[clst];
        fat_value &= 0x0FFFFFFF;
        clst = fat_value;
        tmp += disk->sectors_per_cluster * 512;
    }

cleanup:
    kfree(sec_buf);
    return file_buf;
}

void print_fat32_data(fat_BS_t *ptr)
{
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
