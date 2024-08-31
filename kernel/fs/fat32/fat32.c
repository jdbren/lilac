// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <fs/fat32.h>

#include <lilac/types.h>
#include <lilac/panic.h>
#include <lilac/fs.h>
#include <lilac/file.h>
#include <lilac/list.h>
#include <drivers/blkdev.h>
#include <mm/kmm.h>
#include <mm/kmalloc.h>

#include <string.h>
#include <stdbool.h>

#include "internal.h"

const struct file_operations fat_fops = {
    .read = fat32_read,
    .write = fat32_write,
    .readdir = fat32_readdir
};

const struct super_operations fat_sops = {
    .alloc_inode = fat_alloc_inode,
    .destroy_inode = fat_destroy_inode
};

const struct inode_operations fat_iops = {
    .lookup = fat32_lookup,
    .open = fat32_open,
    .mkdir = fat32_mkdir
};


/**
 *  Utility functions
**/
static void print_fat32_data(struct fat_BS*);

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
    if (fat_disk->fs_info.lead_sig != FAT32_FS_INFO_SIG1 ||
        fat_disk->fs_info.struct_sig != FAT32_FS_INFO_SIG2 ||
        fat_disk->fs_info.trail_sig != FAT32_FS_INFO_TRAIL_SIG)
        return -1;
    return 0;
}

int __must_check
fat32_write_fs_info(struct fat_disk *fat_disk, struct gendisk *gd)
{
    return gd->ops->disk_write(gd, fat_disk->base_lba +
        fat_disk->bpb.extended_section.fs_info, &fat_disk->fs_info, 1);
}

static int __must_check
fat_read_FAT(struct fat_disk *fat_disk, struct gendisk *hd, u32 clst_off)
{
    const u32 lba = fat_disk->fat_begin_lba + clst_off * fat_disk->sect_per_clst;
    const u32 FAT_sz = fat_disk->bpb.extended_section.FAT_size_32;
    const u32 FAT_ent_per_sec = fat_disk->bpb.bytes_per_sector / 4;
    const u32 buf_no_sec = sizeof(fat_disk->FAT.buf) /
        fat_disk->bpb.bytes_per_sector;
    const u32 cnt = clst_off + buf_no_sec > FAT_sz ? FAT_sz - clst_off : buf_no_sec;

    int ret = hd->ops->disk_read(hd, lba, fat_disk->FAT.buf, cnt);

    fat_disk->FAT.first_clst = fat_disk->clst_begin_lba + clst_off;
    fat_disk->FAT.last_clst = fat_disk->clst_begin_lba + (clst_off +
        cnt / fat_disk->sect_per_clst) * FAT_ent_per_sec;
    fat_disk->FAT.sectors = cnt;

    return ret;
}

int __must_check
fat_write_FAT(struct fat_disk *fat_disk, struct gendisk *gd)
{
    const u32 lba = fat_disk->fat_begin_lba +
        ((fat_disk->FAT.first_clst - fat_disk->clst_begin_lba)
        * fat_disk->sect_per_clst);

    return gd->ops->disk_write(gd, lba, fat_disk->FAT.buf, fat_disk->FAT.sectors);
}

inline u32 __get_FAT_val(u32 clst, struct fat_disk *disk)
{
    if (clst > disk->FAT.last_clst) {
        klog(LOG_DEBUG, "clst: %x\n", clst);
        klog(LOG_DEBUG, "last clst: %x\n", disk->FAT.last_clst);
        kerror("clst out of fat bounds\n");
    }
    return disk->FAT.buf[clst] & 0x0FFFFFFF;
}

#define LBA_ADDR(cluster_num, disk) \
    (disk->clst_begin_lba + \
    ((cluster_num - disk->root_start) * disk->sect_per_clst))

inline void __fat_read_clst(struct fat_disk *fat_disk,
    struct gendisk *hd, u32 clst, void *buf)
{
    hd->ops->disk_read(hd, LBA_ADDR(clst, fat_disk), buf,
        fat_disk->sect_per_clst);
}

inline void __fat_write_clst(struct fat_disk *fat_disk,
    struct gendisk *hd, u32 clst, const void *buf)
{
    hd->ops->disk_write(hd, LBA_ADDR(clst, fat_disk), buf,
        fat_disk->sect_per_clst);
}

u32 __fat_get_clst_num(struct file *file, struct fat_disk *disk)
{
    struct fat_file *fat_file = (struct fat_file*)file->f_inode->i_private;
    u32 clst_num = (u32)fat_file->cl_low + ((u32)fat_file->cl_high << 16);
    u32 clst_off = file->f_pos / disk->bytes_per_clst;

    while (clst_off--) {
        if (clst_num > 0x0FFFFFF8)
            return 0;
        if (clst_num > disk->FAT.last_clst) {
            klog(LOG_DEBUG, "clst: %x\n", clst_num);
            klog(LOG_DEBUG, "last clst: %x\n", disk->FAT.last_clst);
            kerror("clst out of fat bounds\n");
        }
        clst_num = disk->FAT.buf[clst_num] & 0x0FFFFFFF;
    }

    return clst_num;
}

u32 __fat_find_free_clst(struct fat_disk *disk)
{
    u32 clst = disk->fs_info.next_free_clst;
    u32 FAT_sz = disk->bpb.extended_section.FAT_size_32;

    if (disk->fs_info.free_clst_cnt == 0)
        return -1;

    while (1) {
        if (clst > disk->FAT.last_clst)
            kerror("clst out of fat bounds\n");
        if (disk->FAT.buf[clst] == 0)
            return clst;
        clst++;
    }

    return -1;
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


/***
 *  FAT32 functions
*/

struct dentry *fat32_init(struct block_device *bdev, struct super_block *sb)
{
    klog(LOG_INFO, "Initializing FAT32 filesystem\n");

    volatile struct fat_disk *fat_disk = kzmalloc(sizeof(*fat_disk));
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
    root_inode->i_type = TYPE_DIR;
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

#ifdef DEBUG_FAT
    print_fat32_data(&fat_disk->bpb);
    klog(LOG_DEBUG, "FSInfo:\n");
    klog(LOG_DEBUG, "Lead sig: %x\n", fat_disk->fs_info.lead_sig);
    klog(LOG_DEBUG, "Struct sig: %x\n", fat_disk->fs_info.struct_sig);
    klog(LOG_DEBUG, "Free clst cnt: %x\n", fat_disk->fs_info.free_clst_cnt);
    klog(LOG_DEBUG, "Next free clst: %x\n", fat_disk->fs_info.next_free_clst);
    klog(LOG_DEBUG, "Trail sig: %x\n", fat_disk->fs_info.trail_sig);
#endif

    return root_dentry;

error:
    kfree(fat_disk);
    kfree(fat_file);
    return NULL;
}

static void print_fat32_data(struct fat_BS *ptr)
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
