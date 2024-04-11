// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <lilac/types.h>
#include <lilac/panic.h>
#include <lilac/fs.h>
#include <drivers/blkdev.h>
#include <fs/fat32.h>
#include <mm/kheap.h>

#define LONG_FNAME 0x0F
#define DIR 0x10
#define UNUSED 0xE5
#define MAX_SECTOR_READS 256
#define BYTES_PER_SECTOR 512
#define FAT_BUFFER_SIZE (BYTES_PER_SECTOR * MAX_SECTOR_READS)

typedef struct fat_disk {
    fat_BS_t volID;
    u32 base_lba;
    u32 fat_begin_lba;
    u32 cluster_begin_lba;
    u32 root_start;
    u8 sectors_per_cluster;
    u32 fat_buffer[FAT_BUFFER_SIZE / 4];
} fat_disk_t;

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

const struct file_operations fat32_fops = {
    .read = &fat32_read,
};


/***
 *  Utility functions
*/
static inline u32 LBA_ADDR(u32 cluster_num, fat_disk_t *disk)
{
    return disk->cluster_begin_lba +
        ((cluster_num - disk->root_start) * disk->sectors_per_cluster);
}

static void disk_init(fat_disk_t *disk)
{
    fat_BS_t *id = &disk->volID;
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

static const char *next_dir_name(const char *path, char *const cur)
{
    if (*path == '/') path++;
    else return 0;

    int dname_len = name_length(path, '/');
    if (dname_len > 8)
        kerror("Pathname too long\n");
    for (int i = 0; i < dname_len; i++)
        cur[i] = toupper(*path++);
    cur[dname_len] = '\0';
    return path;
}

static bool check_entry(fat_file_t *entry, fat_disk_t *disk, const char *cur)
{
    if (entry->attributes != LONG_FNAME && entry->name[0] != (char)UNUSED) {
        if (!memcmp(entry->name, cur, strlen(cur)))
            return true;
    }
    return false;
}


/***
 *  FAT32 functions
*/

int fat32_init(struct vfsmount *mnt)
{
    printf("Initializing FAT32 filesystem\n");

    fat_disk_t *fat_disk = kzmalloc(sizeof(*fat_disk));
    printf("Allocated fat_disk at %x\n", fat_disk);

    struct block_device *bdev = mnt->bdev;
    struct disk_operations *disk_ops = bdev->disk->ops;
    int fat_sectors;

    mnt->private = fat_disk;
    disk_ops->disk_read(bdev->disk, bdev->first_sector, &fat_disk->volID, 1);
    disk_init(fat_disk);
    fat_disk->base_lba = bdev->first_sector;
    print_fat32_data(&fat_disk->volID);
    printf("FAT begin LBA: %x\n", fat_disk->fat_begin_lba);
    printf("Cluster begin LBA: %x\n", fat_disk->cluster_begin_lba);
    printf("Root start: %x\n", fat_disk->root_start);
    printf("Sectors per cluster: %x\n", fat_disk->sectors_per_cluster);

    fat_sectors = fat_disk->volID.extended_section.FAT_size_32;
    fat_sectors = (fat_sectors > MAX_SECTOR_READS) ? MAX_SECTOR_READS : fat_sectors;
    disk_ops->disk_read(bdev->disk, fat_disk->fat_begin_lba, fat_disk->fat_buffer, fat_sectors);
}

static ssize_t __do_fat32_read(struct file *file, void *file_buf, size_t count)
{
    printf("Reading file\n");
    if (count == 0)
        return 0;

    size_t bytes_read = 0;
    struct gendisk *gendisk = file->f_disk->bdev->disk;
    printf("Reading file from disk %s\n", gendisk->driver);
    fat_disk_t *disk = file->f_disk->private;
    fat_file_t *fat_file = (fat_file_t*)file->f_info;
    const int factor = disk->sectors_per_cluster * BYTES_PER_SECTOR;
    printf("Factor: %d\n", factor);
    count = count > factor ? (count / factor) * factor : count;
    printf("Reading %d bytes\n", count);

    u32 clst = (u32)fat_file->cl_low + (u32)fat_file->cl_high;
    u32 fat_value = 0;
    while (fat_value < 0x0FFFFFF8 && bytes_read < count) {
        gendisk->ops->disk_read(gendisk, LBA_ADDR(clst, disk), file_buf, disk->sectors_per_cluster);
        if (clst >= 32768)
            kerror("clst out of fat bounds\n");
        fat_value = disk->fat_buffer[clst] & 0x0FFFFFFF;
        clst = fat_value;
        file_buf = (void*)((u32)file_buf + disk->sectors_per_cluster * BYTES_PER_SECTOR);
        bytes_read += disk->sectors_per_cluster * BYTES_PER_SECTOR;
    }
    return bytes_read;
}

// Read a directory and return a buffer containing the directory entries
static void *fat32_read_dir(fat_file_t *entry, void *buffer, fat_disk_t *disk,
    struct gendisk *hd)
{
    size_t bytes_read = 0;
    u32 clst = (u32)entry->cl_low + (u32)entry->cl_high;
    u32 fat_value = 0;
    void *current;

    while (fat_value < 0x0FFFFFF8) {
        buffer = krealloc(buffer, bytes_read + disk->sectors_per_cluster * BYTES_PER_SECTOR);
        current = (void*)((u32)buffer + bytes_read);
        hd->ops->disk_read(hd, LBA_ADDR(clst, disk), current, disk->sectors_per_cluster);
        if (clst >= 32768)
            kerror("clst out of fat bounds\n");
        fat_value = disk->fat_buffer[clst] & 0x0FFFFFFF;
        clst = fat_value;
        bytes_read += disk->sectors_per_cluster * BYTES_PER_SECTOR;
    }

    return buffer;
}

int fat32_open(const char *path, struct file *file)
{
    char cur[9];
    struct gendisk *gendisk = file->f_disk->bdev->disk;
    fat_disk_t *fat_disk = file->f_disk->private;
    fat_file_t *entry = NULL;
    u8 *sec_buf = kzmalloc(fat_disk->sectors_per_cluster * BYTES_PER_SECTOR);

    printf("Opening file %s\n", path);

    path = next_dir_name(path, cur);
    printf("Current directory: %s\n", cur);

    gendisk->ops->disk_read(gendisk, LBA_ADDR(fat_disk->root_start, fat_disk),
        sec_buf, fat_disk->sectors_per_cluster);
    entry = (fat_file_t*)sec_buf;

    while (entry->name[0] != 0) {
        //printf("Checking entry %s\n", entry->name);
        if (check_entry(entry, fat_disk, cur)) {
            if (*path != '\0') {
                u8 *tmp = kmalloc(fat_disk->sectors_per_cluster * BYTES_PER_SECTOR);
                tmp = fat32_read_dir(entry, tmp, fat_disk, gendisk);
                kfree(sec_buf);
                sec_buf = tmp;
                entry = (fat_file_t*)sec_buf;
                path = next_dir_name(path, cur);
                continue;
            }
            else {
                kfree(sec_buf);
                file->f_info = entry;
                file->f_op = &fat32_fops;
                return 0;
            }
        }
        entry++;
    }
    kfree(sec_buf);
    printf("File not found\n");
    return -1;
}

ssize_t fat32_read(struct file *file, void *file_buf, size_t count)
{
    printf("Reading %d bytes from file\n", count);
    return __do_fat32_read(file, file_buf, count);
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
    printf("Root cluster: %x\n", ptr->extended_section.root_cluster);
    printf("FS info: %x\n", ptr->extended_section.fs_info);
    printf("Backup BS sector: %x\n", ptr->extended_section.backup_BS_sector);
    printf("Drive number: %x\n", ptr->extended_section.drive_number);
    printf("Volume ID: %x\n", ptr->extended_section.volume_id);
    printf("Signature: %x\n", ptr->extended_section.signature);
}
