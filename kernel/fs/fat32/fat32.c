// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <fs/fat32.h>

#include <stdbool.h>
#include <lilac/types.h>
#include <lilac/panic.h>
#include <lilac/fs.h>
#include <lib/list.h>
#include <lilac/libc.h>
#include <drivers/blkdev.h>
#include <mm/kmm.h>
#include <mm/kmalloc.h>
#include <mm/page.h>

#include "fat_internal.h"

#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"

// #define DEBUG_FAT 1

const struct file_operations fat_fops = {
    .read = fat32_read,
    .write = fat32_write,
    .readdir = fat32_readdir,
    .release = fat32_close,
};

const struct super_operations fat_sops = {
    .alloc_inode = fat_alloc_inode,
    .destroy_inode = fat_destroy_inode
};

const struct inode_operations fat_iops = {
    .lookup = fat32_lookup,
    .open = fat32_open,
    .mkdir = fat32_mkdir,
    .create = fat32_create,
};


/**
 *  Utility functions
**/
static void print_fat32_data(volatile struct fat_BS*);

__must_check
static inline int fat_read_bpb(struct fat_disk *fat_disk, struct gendisk *gd)
{
    gd->ops->disk_read(gd, fat_disk->base_lba, (void*)&fat_disk->bpb, 1);
    if (fat_disk->bpb.extended_section.signature != 0xAA55)
        return -1;
    return 0;
}

__must_check
static inline int fat32_read_fs_info(struct fat_disk *fat_disk, struct gendisk *gd)
{
    gd->ops->disk_read(gd, fat_disk->base_lba +
        fat_disk->bpb.extended_section.fs_info, (void*)&fat_disk->fs_info, 1);
    if (fat_disk->fs_info.lead_sig != FAT32_FS_INFO_SIG1 ||
        fat_disk->fs_info.struct_sig != FAT32_FS_INFO_SIG2 ||
        fat_disk->fs_info.trail_sig != FAT32_FS_INFO_TRAIL_SIG)
        return -1;
    return 0;
}

__must_check
int fat32_write_fs_info(struct fat_disk *fat_disk, struct gendisk *gd)
{
    return gd->ops->disk_write(gd, fat_disk->base_lba +
        fat_disk->bpb.extended_section.fs_info, (void*)&fat_disk->fs_info, 1);
}

__must_check
static int fat_read_FAT(struct fat_disk *fat_disk, struct gendisk *hd)
{
    const u32 lba = fat_disk->fat_begin_lba + 0 * fat_disk->sect_per_clst;
    const u32 FAT_sz = fat_disk->bpb.extended_section.FAT_size_32;
    const u32 buf_sz = fat_disk->bpb.bytes_per_sector * FAT_sz;

    klog(LOG_INFO, "Allocating %u bytes for FAT\n", buf_sz);
    fat_disk->FAT.FAT_buf = get_zeroed_pages(PAGE_UP_COUNT(buf_sz), 0);

    int ret = 0;
    for (u32 i = 0; i < FAT_sz; i += 128) {
        if (i + 128 > FAT_sz) {
            // Last read might be less than 128 sectors
            ret = hd->ops->disk_read(hd, lba + i,
                (void*)fat_disk->FAT.FAT_buf + (i * 512), FAT_sz - i);
            break;
        }
        ret = hd->ops->disk_read(hd, lba + i,
            (void*)fat_disk->FAT.FAT_buf + (i * 512), 128);
    }

    fat_disk->FAT.first_clst = 0;
    fat_disk->FAT.last_clst = fat_disk->bpb.total_sectors_32;
    fat_disk->FAT.sectors = FAT_sz;

#ifdef DEBUG_FAT
    klog(LOG_DEBUG, "FAT first: %x\n", fat_disk->FAT.first_clst);
    klog(LOG_DEBUG, "FAT last: %x\n", fat_disk->FAT.last_clst);
#endif
    return ret;
}

__must_check
int fat_write_FAT(struct fat_disk *fat_disk, struct gendisk *gd)
{
    const u32 lba = fat_disk->fat_begin_lba +
        (fat_disk->FAT.first_clst * fat_disk->sect_per_clst);
    u32 i = 0;
    while (i < fat_disk->FAT.sectors) {
        if (i + 128 > fat_disk->FAT.sectors) {
            // Last write might be less than 128 sectors
            gd->ops->disk_write(gd, lba + i,
                (void*)fat_disk->FAT.FAT_buf + (i * 512), fat_disk->FAT.sectors - i);
            break;
        }
        gd->ops->disk_write(gd, lba + i,
            (void*)fat_disk->FAT.FAT_buf + (i * 512), 128);
        i += 128;
    }
    return 0;
}

#define LBA_ADDR(cluster_num, disk) \
    (disk->clst_begin_lba + \
    ((cluster_num - disk->root_start) * disk->sect_per_clst))

void __fat_read_clst(struct fat_disk *fat_disk,
    struct gendisk *hd, u32 clst, void *buf)
{
    hd->ops->disk_read(hd, LBA_ADDR(clst, fat_disk), buf,
        fat_disk->sect_per_clst);
}

void __fat_write_clst(struct fat_disk *fat_disk,
    struct gendisk *hd, u32 clst, const void *buf)
{
    hd->ops->disk_write(hd, LBA_ADDR(clst, fat_disk), buf,
        fat_disk->sect_per_clst);
}

int __fat_get_clst_num(struct file *file, struct fat_disk *disk)
{
    struct fat_file *fat_file = (struct fat_file*)file->f_dentry->d_inode->i_private;
    u32 clst_num = fat_clst_value(fat_file);
    u32 clst_off = file->f_pos / disk->bytes_per_clst;

    while (clst_off--) {
        if (clst_num > 0x0FFFFFF8)
            return -1;
        clst_num = fat_value(clst_num, disk);
    }

    return clst_num;
}

int __fat_find_free_clst(struct fat_disk *disk)
{
    u32 clst = disk->fs_info.next_free_clst;

    if (disk->fs_info.free_clst_cnt == 0)
        return -1;

    while (1) {
        if (clst > disk->FAT.last_clst)
            kerror("clst out of fat bounds\n");
        if (fat_value(clst, disk) == 0)
            return clst;
        clst++;
    }

    return -1;
}

int __fat_add_new_clst(struct fat_disk *disk, u32 prev_clst, u32 new_clst)
{
    if (prev_clst != 0)
        fat_set_value(prev_clst, new_clst, disk);
    fat_set_value(new_clst, 0x0FFFFFFF, disk); // Mark new clst as EOF

    disk->fs_info.free_clst_cnt--;
    disk->fs_info.next_free_clst = __fat_find_free_clst(disk);
    return new_clst;
}

int __fat_find_alloc_clst(struct fat_disk *disk, u32 prev_clst)
{
    int new_clst = __fat_find_free_clst(disk);
    if (new_clst == -1)
        return -1;
    return __fat_add_new_clst(disk, prev_clst, new_clst);
}

static void disk_init(struct fat_disk *disk)
{
    volatile struct fat_BS *id = &disk->bpb;
    disk->fat_begin_lba = disk->base_lba + id->reserved_sector_count;
    disk->clst_begin_lba = disk->base_lba + id->reserved_sector_count
        + (id->num_FATs * id->extended_section.FAT_size_32);
    disk->sect_per_clst = id->sectors_per_cluster;
    disk->bytes_per_clst = id->sectors_per_cluster * id->bytes_per_sector;
    disk->root_start = id->extended_section.root_cluster;
}


struct dentry *fat32_init(void *dev, struct super_block *sb)
{
    klog(LOG_INFO, "Initializing FAT32 filesystem\n");

    struct block_device *bdev = (struct block_device*)dev;
    struct fat_disk *fat_disk = kzmalloc(sizeof(*fat_disk));
    struct fat_inode *fat_inode = kzmalloc(sizeof(*fat_inode));
    struct inode *root_inode;
    struct dentry *root_dentry;

    if (!fat_disk || !fat_inode) {
        kerror("Out of memory allocating FAT32 structures\n");
    }

    // Initialize the FAT32 disk info
    fat_disk->base_lba = bdev->first_sector_lba;
    fat_disk->bdev = bdev;
    if(fat_read_bpb(fat_disk, bdev->disk))
        goto error;
    if(fat32_read_fs_info(fat_disk, bdev->disk))
        goto error;
    disk_init(fat_disk);

    // Initialize the root inode
    root_inode = fat_alloc_inode(sb);
    root_inode->i_ino = 1;
    root_inode->i_private = fat_inode;
    root_inode->i_mode |= S_IFDIR;
    fat_inode->entry.cl_low = fat_disk->root_start & 0xFFFF;
    fat_inode->entry.cl_high = fat_disk->root_start >> 16;
    fat_inode->entry.attributes = FAT_DIR_ATTR;

    // Initialize the dentry
    root_dentry = kzmalloc(sizeof(struct dentry));
    if (!root_dentry) {
        kerror("Out of memory allocating FAT32 root dentry\n");
    }
    root_dentry->d_sb = sb;
    root_dentry->d_inode = root_inode;
    root_dentry->d_count = 1;

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
    sb->s_fs_info = fat_disk;
    strncpy(bdev->name, (const char*)fat_disk->bpb.extended_section.volume_label, 11);

    // Read the FAT table
    if (fat_read_FAT(fat_disk, bdev->disk))
        kerror("Failed to read FAT\n");

#ifdef DEBUG_FAT
    print_fat32_data(&fat_disk->bpb);
    klog(LOG_DEBUG, "FSInfo:\n");
    klog(LOG_DEBUG, "Lead sig: %x\n", fat_disk->fs_info.lead_sig);
    klog(LOG_DEBUG, "Struct sig: %x\n", fat_disk->fs_info.struct_sig);
    klog(LOG_DEBUG, "Free clst cnt: %x\n", fat_disk->fs_info.free_clst_cnt);
    klog(LOG_DEBUG, "Next free clst: %x\n", fat_disk->fs_info.next_free_clst);
    klog(LOG_DEBUG, "Trail sig: %x\n", fat_disk->fs_info.trail_sig);
    klog(LOG_DEBUG, "Root start: %x\n", fat_disk->root_start);
    klog(LOG_DEBUG, "FAT begin: %x\n", fat_disk->fat_begin_lba);
    klog(LOG_DEBUG, "Clst begin: %x\n", fat_disk->clst_begin_lba);
#endif

    return root_dentry;

error:
    kfree(fat_disk);
    kfree(fat_inode);
    return NULL;
}

static void print_fat32_data(volatile struct fat_BS *ptr)
{
    klog(LOG_DEBUG, "FAT32 data:\n");
    klog(LOG_DEBUG, "Bytes per sector: %x\n", ptr->bytes_per_sector);
    klog(LOG_DEBUG, "Sectors per cluster: %x\n", ptr->sectors_per_cluster);
    klog(LOG_DEBUG, "Reserved sector count: %x\n", ptr->reserved_sector_count);
    klog(LOG_DEBUG, "Table count: %x\n", ptr->num_FATs);
    klog(LOG_DEBUG, "Root entry count: %x\n", ptr->root_entry_count);
    klog(LOG_DEBUG, "Media type: %x\n", ptr->media_type);
    klog(LOG_DEBUG, "Hidden sector count: %x\n", ptr->hidden_sector_count);
    klog(LOG_DEBUG, "Total sectors 32: %x\n", ptr->total_sectors_32);

    klog(LOG_DEBUG, "FAT32 extended data:\n");
    klog(LOG_DEBUG, "FAT size 32: %x\n", ptr->extended_section.FAT_size_32);
    klog(LOG_DEBUG, "Root cluster: %x\n", ptr->extended_section.root_cluster);
    klog(LOG_DEBUG, "FS info: %x\n", ptr->extended_section.fs_info);
    klog(LOG_DEBUG, "Backup BS sector: %x\n", ptr->extended_section.backup_BS_sector);
    klog(LOG_DEBUG, "Drive number: %x\n", ptr->extended_section.drive_number);
    klog(LOG_DEBUG, "Volume ID: %x\n", ptr->extended_section.volume_id);
    klog(LOG_DEBUG, "Signature: %x\n", ptr->extended_section.signature);
}
