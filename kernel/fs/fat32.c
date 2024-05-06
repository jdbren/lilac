// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <lilac/types.h>
#include <lilac/panic.h>
#include <lilac/fs.h>
#include <lilac/list.h>
#include <drivers/blkdev.h>
#include <fs/fat32.h>
#include <mm/kheap.h>

#define LONG_FNAME 0x0F
#define DIR 0x10
#define UNUSED 0xE5
#define MAX_SECTOR_READS 64
#define BYTES_PER_SECTOR 512
#define FAT_BUFFER_SIZE (BYTES_PER_SECTOR * MAX_SECTOR_READS)

struct fat_FAT_buf {
    u32 first_clst;
    u32 last_clst;
    u32 buf[FAT_BUFFER_SIZE / 4];
};

struct fat_disk {
    u32 base_lba;
    u32 fat_begin_lba;
    u32 clst_begin_lba;
    u32 root_start;
    u16 bytes_per_clst;
    u16 sect_per_clst;
    struct fat_BS bpb;
    struct FSInfo fs_info;
    struct fat_FAT_buf FAT;
};

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

const struct file_operations fat_fops = {
    .read = fat32_read,
    .write = fat32_write
};

const struct super_operations fat_sops = {
    .alloc_inode = fat_alloc_inode,
    .destroy_inode = fat_destroy_inode
};

const struct inode_operations fat_iops = {
    .lookup = fat32_lookup,
    .open = fat32_open
};

struct inode *fat_alloc_inode(struct super_block *sb)
{
    struct inode *new_node = kzmalloc(sizeof(struct inode));

    new_node->i_sb = sb;
    new_node->i_op = &fat_iops;
    new_node->i_count = 1;

    return new_node;
}

void fat_destroy_inode(struct inode *inode)
{
    kfree(inode);
}


/***
 *  Utility functions
*/
static inline u32 LBA_ADDR(u32 cluster_num, struct fat_disk *disk)
{
    return disk->clst_begin_lba +
        ((cluster_num - disk->root_start) * disk->sect_per_clst);
}

static inline int __must_check
fat_read_bpb(struct fat_disk *fat_disk, struct gendisk *gd)
{
    gd->ops->disk_read(gd, fat_disk->base_lba, &fat_disk->bpb, 1);
    if (fat_disk->bpb.extended_section.signature != 0xAA55)
        return -1;
    return 0;
}

static inline int __must_check
fat32_read_fs_info(struct fat_disk *fat_disk, struct gendisk *gd)
{
    gd->ops->disk_read(gd, fat_disk->base_lba +
        fat_disk->bpb.extended_section.fs_info, &fat_disk->fs_info, 1);
    if (fat_disk->fs_info.lead_sig != 0x41615252 ||
        fat_disk->fs_info.struct_sig != 0x61417272 ||
        fat_disk->fs_info.trail_sig != 0xAA550000)
        return -1;
    return 0;
}

static void fat_read_FAT(struct fat_disk *fat_disk, struct gendisk *hd,
    u32 clst_off)
{
    const u32 lba = fat_disk->fat_begin_lba + clst_off * fat_disk->sect_per_clst;
    const u32 FAT_sz = fat_disk->bpb.extended_section.FAT_size_32;
    const u32 FAT_ent_per_sec = fat_disk->bpb.bytes_per_sector / 4;
    const u32 buf_no_sec = sizeof(fat_disk->FAT.buf) /
        fat_disk->bpb.bytes_per_sector;
    u32 cnt = clst_off + buf_no_sec > FAT_sz ? FAT_sz - clst_off : buf_no_sec;

    hd->ops->disk_read(hd, lba, fat_disk->FAT.buf, cnt);

    fat_disk->FAT.first_clst = fat_disk->clst_begin_lba + clst_off;
    fat_disk->FAT.last_clst = fat_disk->clst_begin_lba + (clst_off +
        cnt / fat_disk->sect_per_clst) * FAT_ent_per_sec;
}

static inline void __fat_read_clst(struct fat_disk *fat_disk,
    struct gendisk *hd, u32 clst, void *buf)
{
    hd->ops->disk_read(hd, LBA_ADDR(clst, fat_disk), buf,
        fat_disk->sect_per_clst);
}

static inline void __fat_write_clst(struct fat_disk *fat_disk,
    struct gendisk *hd, u32 clst, void *buf)
{
    hd->ops->disk_write(hd, LBA_ADDR(clst, fat_disk), buf,
        fat_disk->sect_per_clst);
}

static void disk_init(struct fat_disk *disk)
{
    struct fat_BS *id = &disk->bpb;
    disk->fat_begin_lba = id->hidden_sector_count + id->reserved_sector_count;
    disk->clst_begin_lba = id->hidden_sector_count + id->reserved_sector_count
        + (id->num_FATs * id->extended_section.FAT_size_32);
    disk->sect_per_clst = id->sectors_per_cluster;
    disk->bytes_per_clst = id->sectors_per_cluster * id->bytes_per_sector;
    disk->root_start = id->extended_section.root_cluster;
}

static bool check_entry(struct fat_file *entry, const char *cur)
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

struct dentry *fat32_init(struct block_device *bdev, struct super_block *sb)
{
    printf("Initializing FAT32 filesystem\n");

    struct fat_disk *fat_disk = kzmalloc(sizeof(*fat_disk));
    struct fat_file *fat_file = kzmalloc(sizeof(*fat_file));
    struct disk_operations *disk_ops = bdev->disk->ops;
    int fat_sectors;
    struct inode *root_inode;
    struct dentry *root_dentry;

    // Initialize the FAT32 disk info
    fat_disk->base_lba = bdev->first_sector_lba;
    if(fat_read_bpb(fat_disk, bdev->disk))
        goto error;
    if(fat32_read_fs_info(fat_disk, bdev->disk))
        goto error;
    disk_init(fat_disk);

    // Initialize the root inode
    root_inode = fat_alloc_inode(sb);
    root_inode->i_ino = 1;
    root_inode->i_private = fat_file;
    fat_file->cl_low = fat_disk->root_start & 0xFFFF;
    fat_file->cl_high = fat_disk->root_start >> 16;

    // Initialize the dentry
    root_dentry = kzmalloc(sizeof(struct dentry));
    root_dentry->d_sb = sb;
    root_dentry->d_inode = root_inode;

    // Initialize the super block
    sb->s_blocksize = fat_disk->bytes_per_clst;
    sb->s_maxbytes = 0xFFFFFFFF;
    sb->s_type = MSDOS;
    sb->s_op = &fat_sops;
    sb->s_root = root_dentry;
    sb->s_bdev = bdev;
    atomic_store(&sb->s_active, true);
    INIT_LIST_HEAD(&sb->s_inodes);
    list_add(&root_inode->i_list, &sb->s_inodes);
    sb->private = fat_disk;
    strncpy(bdev->name, fat_disk->bpb.extended_section.volume_label, 11);

    // Read the FAT table
    fat_read_FAT(fat_disk, bdev->disk, 0);

    return root_dentry;

error:
    kfree(fat_disk);
    kfree(fat_file);
    return NULL;
}

static ssize_t __do_fat32_read(struct inode *inode, void *file_buf, size_t count)
{
    if (count == 0)
        return 0;

    size_t bytes_read = 0;
    struct gendisk *gd = inode->i_sb->s_bdev->disk;
    struct fat_disk *fat_disk = (struct fat_disk*)inode->i_sb->private;
    struct fat_file *fat_file = (struct fat_file*)inode->i_private;
    const size_t factor = fat_disk->bytes_per_clst;
    count = count > factor ? (count / factor) * factor : count;

    u32 clst = (u32)fat_file->cl_low + (u32)fat_file->cl_high;
    u32 fat_value = 0;
    while (fat_value < 0x0FFFFFF8 && bytes_read < count) {
        __fat_read_clst(fat_disk, gd, clst, file_buf);
        if (clst > fat_disk->FAT.last_clst) {
            printf("clst: %x\n", clst);
            printf("last clst: %x\n", fat_disk->FAT.last_clst);
            kerror("clst out of fat bounds\n");
        }
        fat_value = fat_disk->FAT.buf[clst] & 0x0FFFFFFF;
        clst = fat_value;
        file_buf = (void*)((u32)file_buf + fat_disk->bytes_per_clst);
        bytes_read += fat_disk->bytes_per_clst;
    }

    return bytes_read;
}

static ssize_t __do_fat32_write(struct inode *inode, const void *buf, size_t count)
{
    ssize_t bytes_written = 0;
    struct gendisk *gd = inode->i_sb->s_bdev->disk;
    struct fat_disk *fat_disk = (struct fat_disk*)inode->i_sb->private;
    struct fat_file *fat_file = (struct fat_file*)inode->i_private;
    const size_t factor = fat_disk->bytes_per_clst;
    count = count > factor ? (count / factor) * factor : count;
    u32 clst = (u32)fat_file->cl_low + (u32)fat_file->cl_high;

    gd->ops->disk_write(gd, LBA_ADDR(clst, fat_disk), buf,
        fat_disk->sect_per_clst);

    return count;
}

// Read a directory and return a buffer containing the directory entries
static void *fat32_read_dir(struct fat_file *entry, struct fat_disk *disk,
    struct gendisk *hd)
{
    size_t bytes_read = 0;
    u32 clst = (u32)entry->cl_low + (u32)entry->cl_high;
    u32 fat_value = 0;
    unsigned char *current;
    unsigned char *buffer = kzmalloc(disk->bytes_per_clst);

    while (fat_value < 0x0FFFFFF8) {
        buffer = krealloc(buffer, bytes_read + disk->bytes_per_clst);
        current = buffer + bytes_read;
        __fat_read_clst(disk, hd, clst, current);
        if (clst > disk->FAT.last_clst) {
            printf("clst: %x\n", clst);
            printf("last clst: %x\n", disk->FAT.last_clst);
            kerror("clst out of fat bounds\n");
        }
        fat_value = disk->FAT.buf[clst] & 0x0FFFFFFF;
        clst = fat_value;
        bytes_read += disk->bytes_per_clst;
    }

    return buffer;
}

int fat32_open(struct inode *inode, struct file *file)
{
    printf("Opening file %s\n", file->f_path);
    file->f_inode = inode;
    file->f_op = &fat_fops;
    file->f_count++;
    return 0;
}

ssize_t fat32_read(struct file *file, void *file_buf, size_t count)
{
    return __do_fat32_read(file->f_inode, file_buf, count);
}

ssize_t fat32_write(struct file *file, const void *file_buf, size_t count)
{
    return __do_fat32_write(file->f_inode, file_buf, count);
}

void print_fat32_data(struct fat_BS *ptr)
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

static int unique_ino(void)
{
	static unsigned long ino = 1;
	return ++ino;
}

static struct inode *fat_iget(struct super_block *sb, unsigned long pos)
{
    struct inode *tmp;
    struct fat_file *info;
    unsigned long i_pos;

    list_for_each_entry(tmp, &sb->s_inodes, i_list) {
        info = (struct fat_file*)tmp->i_private;
        i_pos = (u32)info->cl_low + (u32)info->cl_high;
        if (i_pos == pos)
            return tmp;
    }

    return NULL;
}

static struct inode *fat_build_inode(struct super_block *sb, struct fat_file *info)
{
    struct inode *inode;
    unsigned long pos = (u32)info->cl_low + (u32)info->cl_high;

    inode = fat_iget(sb, pos);
    if (inode) {
        kfree(info);
        return inode;
    }

    inode = fat_alloc_inode(sb);

    inode->i_ino = unique_ino();
    inode->i_size = info->file_size;
    inode->i_private = info;

    list_add_tail(&inode->i_list, &sb->s_inodes);

    return inode;
}

static int fat32_find(struct inode *dir, const char *name,
    struct fat_file *info)
{
    int ret = 0;
    struct fat_file *entry = (struct fat_file*)dir->i_private;
    struct fat_disk *disk = (struct fat_disk*)dir->i_sb->private;
    unsigned char *buffer;

    buffer = fat32_read_dir(entry, disk, dir->i_sb->s_bdev->disk);
    entry = (struct fat_file*)buffer;

    while (entry->name[0] != 0) {
        if (check_entry(entry, name)) {
            memcpy(info, entry, sizeof(*info));
            break;
        }
        entry++;
    }

    kfree(buffer);
    return ret;
}

struct dentry *fat32_lookup(struct inode *parent, struct dentry *find,
    unsigned int flags)
{
    struct inode *inode;
    struct fat_file *info = kzmalloc(sizeof(*info));
    char name[9];

    for (int i = 0; i < 8; i++)
        name[i] = toupper(find->d_name[i]);
    name[8] = '\0';

    if (fat32_find(parent, name, info) == 0) {
        inode = fat_build_inode(parent->i_sb, info);
        find->d_inode = inode;
        inode->i_count++;
    }

    return NULL;
}
