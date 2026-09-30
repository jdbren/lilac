// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <fs/fat32.h>

#include <stdbool.h>
#include <lilac/types.h>
#include <lilac/panic.h>
#include <lilac/fs.h>
#include <lib/list.h>
#include <lilac/libc.h>
#include <lilac/timer.h>
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

static int fat_zero_clsts(struct fat_disk *disk, struct gendisk *gd,
    u32 first, u32 count, const void *zero);

// Add zeroed clusters until the chain covers size bytes
static int fat32_grow(struct inode *inode, loff_t size)
{
    struct fat_inode *fi = (struct fat_inode*)inode->i_private;
    struct fat_disk *disk = (struct fat_disk*)inode->i_sb->s_fs_info;
    struct gendisk *gd = inode->i_sb->s_bdev->disk;
    const u32 bpc = disk->bytes_per_clst;
    // one disk request moves at most 128 sectors
    const u32 max_run = MAX(128U / disk->sect_per_clst, 1U);
    u32 need = ROUND_UP(size, bpc) / bpc;
    u32 have = 0, last = 0;
    int err = 0;

    for (u32 c = fat_clst_value(&fi->entry); c >= 2 && c < 0x0FFFFFF8; c = fat_value(c, disk)) {
        last = c;
        have++;
    }
    if (have >= need)
        return 0;

    void *zero = get_zeroed_pages(PAGE_UP_COUNT(max_run * bpc), 0);
    if (!zero)
        return -ENOMEM;

    const u32 old_last = last;
    u32 run_start = 0, run_len = 0;
    for (; have < need; have++) {
        u32 c = __fat_find_alloc_clst(disk, last);
        if (c == 0) {
            err = -ENOSPC;
            break;
        }
        if (last == 0) {
            fi->entry.cl_low = c & 0xFFFF;
            fi->entry.cl_high = c >> 16;
        }
        last = c;
        if (run_len && (c != run_start + run_len || run_len == max_run)) {
            if ((err = fat_zero_clsts(disk, gd, run_start, run_len, zero)))
                break;
            run_len = 0;
        }
        if (run_len++ == 0)
            run_start = c;
    }
    if (!err && run_len)
        err = fat_zero_clsts(disk, gd, run_start, run_len, zero);
    free_pages(zero, PAGE_UP_COUNT(max_run * bpc));

    if (err) {
        u32 added = old_last ? fat_value(old_last, disk) : fat_clst_value(&fi->entry);
        if (old_last) {
            fat_set_value(old_last, 0x0FFFFFFF, disk);
        } else {
            fi->entry.cl_low = 0;
            fi->entry.cl_high = 0;
        }
        fat_free_chain(disk, added);
    }
    int ferr = fat_write_FAT(disk, gd);
    return err ? err : ferr;
}

// Shrink or grow the file
static int fat32_truncate(struct inode *inode, loff_t size)
{
    struct fat_inode *fi = (struct fat_inode*)inode->i_private;
    struct fat_disk *disk = (struct fat_disk*)inode->i_sb->s_fs_info;
    struct gendisk *gd = inode->i_sb->s_bdev->disk;
    const u32 bpc = disk->bytes_per_clst;
    int err = 0;

    if (size > fi->entry.file_size) {
        if ((err = fat32_grow(inode, size))) {
            fat_write_inode(inode); // the first cluster may be back to 0
            return err;
        }
    } else if (size < fi->entry.file_size) {
        u32 keep = ROUND_UP(size, bpc) / bpc;
        u32 clst = fat_clst_value(&fi->entry);

        if (keep == 0) {
            fat_free_chain(disk, clst);
            fi->entry.cl_low = 0;
            fi->entry.cl_high = 0;
        } else if (clst >= 2) {
            for (u32 i = 1; i < keep && clst < 0x0FFFFFF8; i++)
                clst = fat_value(clst, disk);
            if (clst < 0x0FFFFFF8) {
                u32 next = fat_value(clst, disk);
                if (next < 0x0FFFFFF8) {
                    fat_set_value(clst, 0x0FFFFFFF, disk);
                    fat_free_chain(disk, next);
                }
                // clear the cut-off tail so a later extension reads zeros
                if (size % bpc) {
                    u8 *buf = kmalloc(bpc);
                    if (!buf)
                        err = -ENOMEM;
                    else if (!(err = __fat_read_clst(disk, gd, clst, buf))) {
                        memset(buf + size % bpc, 0, bpc - size % bpc);
                        err = __fat_write_clst(disk, gd, clst, buf);
                    }
                    kfree(buf);
                }
            }
        }
        int ferr = fat_write_FAT(disk, gd);
        if (!err)
            err = ferr;
    }

    fi->entry.file_size = size;
    inode->i_size = size;
    int werr = fat_write_inode(inode);
    return err ? err : werr;
}

const struct inode_operations fat_iops = {
    .lookup = fat32_lookup,
    .open = fat32_open,
    .mkdir = fat32_mkdir,
    .create = fat32_create,
    .truncate = fat32_truncate,
    .unlink = fat32_unlink,
    .rmdir = fat32_rmdir,
    .rename = fat32_rename,
};


/**
 *  Utility functions
**/
#ifdef DEBUG_FAT
static void print_fat32_data(volatile struct fat_BS*);
#endif

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

// With mirroring disabled (bit 7 of the extended flags) only the active FAT,
// named by bits 0-3, is in use; otherwise all copies are kept identical
#define FAT_NO_MIRROR 0x80
#define FAT_ACTIVE_MASK 0x0F

static void fat_copies(struct fat_disk *fat_disk, u32 *first, u32 *end)
{
    u16 flags = fat_disk->bpb.extended_section.extended_flags;
    if (flags & FAT_NO_MIRROR) {
        *first = flags & FAT_ACTIVE_MASK;
        *end = *first + 1;
    } else {
        *first = 0;
        *end = fat_disk->bpb.num_FATs;
    }
}

__must_check
static int fat_read_FAT(struct fat_disk *fat_disk, struct gendisk *hd)
{
    const u32 FAT_sz = fat_disk->bpb.extended_section.FAT_size_32;
    u32 active, end;
    fat_copies(fat_disk, &active, &end);
    const u32 lba = fat_disk->fat_begin_lba + active * FAT_sz;
    const u32 buf_sz = fat_disk->bpb.bytes_per_sector * FAT_sz;

    klog(LOG_INFO, "Allocating %u bytes for FAT\n", buf_sz);
    fat_disk->FAT.FAT_buf = get_zeroed_pages(PAGE_UP_COUNT(buf_sz), 0);
    if (!fat_disk->FAT.FAT_buf)
        return -ENOMEM;

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

    volatile struct fat_BS *bpb = &fat_disk->bpb;
    u32 data_sectors = bpb->total_sectors_32 -
        (fat_disk->clst_begin_lba - fat_disk->base_lba);
    u32 last = data_sectors / bpb->sectors_per_cluster + 1;
    fat_disk->FAT.first_clst = 0;
    fat_disk->FAT.last_clst = MIN(last, buf_sz / (u32)sizeof(u32) - 1);
    fat_disk->FAT.sectors = FAT_sz;
    fat_disk->FAT.dirty_lo = (u32)-1;
    fat_disk->FAT.dirty_hi = 0;

#ifdef DEBUG_FAT
    klog(LOG_DEBUG, "FAT first: %x\n", fat_disk->FAT.first_clst);
    klog(LOG_DEBUG, "FAT last: %x\n", fat_disk->FAT.last_clst);
#endif
    return ret;
}

__must_check
int fat_write_FAT(struct fat_disk *fat_disk, struct gendisk *gd)
{
    struct fat_FAT_buf *FAT = &fat_disk->FAT;
    int ret = 0;

    if (FAT->dirty_lo > FAT->dirty_hi)
        return 0;

    const u32 lo = FAT->dirty_lo;
    const u32 count = FAT->dirty_hi - lo + 1;
    u32 first, end;
    fat_copies(fat_disk, &first, &end);
    for (u32 f = first; f < end; f++) {
        u32 lba = fat_disk->fat_begin_lba + f * FAT->sectors + lo;
        for (u32 i = 0; i < count; i += 128) {
            int err = gd->ops->disk_write(gd, lba + i,
                (void*)FAT->FAT_buf + (lo + i) * BYTES_PER_SECTOR, MIN(128U, count - i));
            if (err < 0)
                ret = err;
        }
    }
    FAT->dirty_lo = (u32)-1;
    FAT->dirty_hi = 0;

    int err = fat32_write_fs_info(fat_disk, gd);
    if (err < 0 && !ret)
        ret = err;
    return ret;
}

#define LBA_ADDR(cluster_num, disk) \
    (disk->clst_begin_lba + \
    ((cluster_num - disk->root_start) * disk->sect_per_clst))

static inline bool fat_bad_data_clst(u32 clst)
{
    if (clst >= 2)
        return false;
    klog(LOG_ERROR, "fat: I/O to reserved cluster %u refused\n", clst);
    return true;
}

int __fat_read_clst(struct fat_disk *fat_disk,
    struct gendisk *hd, u32 clst, void *buf)
{
    if (fat_bad_data_clst(clst))
        return -EIO;
    int err = hd->ops->disk_read(hd, LBA_ADDR(clst, fat_disk), buf,
        fat_disk->sect_per_clst);
    return err < 0 ? err : 0;
}

int __fat_write_clst(struct fat_disk *fat_disk,
    struct gendisk *hd, u32 clst, const void *buf)
{
    if (fat_bad_data_clst(clst))
        return -EIO;
    int err = hd->ops->disk_write(hd, LBA_ADDR(clst, fat_disk), buf,
        fat_disk->sect_per_clst);
    return err < 0 ? err : 0;
}

// Clear count consecutive clusters from first; zero holds up to 128 sectors
static int fat_zero_clsts(struct fat_disk *disk, struct gendisk *gd,
    u32 first, u32 count, const void *zero)
{
    if (fat_bad_data_clst(first))
        return -EIO;
    int err = gd->ops->disk_write(gd, LBA_ADDR(first, disk), zero,
        count * disk->sect_per_clst);
    return err < 0 ? err : 0;
}

// Allocate a cluster after prev (0 = none) and clear it on disk, so file
// holes and space added by truncate read back as zeros
u32 fat_alloc_zeroed_clst(struct fat_disk *disk, struct gendisk *hd, u32 prev)
{
    void *zero = kzmalloc(disk->bytes_per_clst);
    if (!zero)
        return 0;
    u32 clst = __fat_find_alloc_clst(disk, prev);
    if (clst && __fat_write_clst(disk, hd, clst, zero)) {
        if (prev)
            fat_set_value(prev, 0x0FFFFFFF, disk);
        fat_free_chain(disk, clst);
        clst = 0;
    }
    kfree(zero);
    return clst;
}

// Write the inode's size and first cluster back to its directory entry and
// stamp it as modified now
int fat_write_inode(struct inode *inode)
{
    struct fat_inode *fi = (struct fat_inode*)inode->i_private;
    struct fat_disk *disk = (struct fat_disk*)inode->i_sb->s_fs_info;
    struct gendisk *gd = inode->i_sb->s_bdev->disk;
    const u32 per_clst = disk->bytes_per_clst / sizeof(struct fat_file);
    struct timestamp now = get_timestamp();
    u32 clst = fi->dir_clst;

    fi->entry.last_write_date = FAT_SET_DATE(now.year, now.month, now.day);
    fi->entry.last_write_time = FAT_SET_TIME(now.hour, now.minute, now.second);
    fi->entry.last_access_date = fi->entry.last_write_date;

    if (clst == 0)
        return 0;

    for (u32 n = fi->dir_idx / per_clst; n; n--) {
        clst = fat_value(clst, disk);
        if (clst < 2 || clst >= 0x0FFFFFF8)
            return -EIO;
    }

    u32 off = (fi->dir_idx % per_clst) * sizeof(struct fat_file);
    u32 lba = LBA_ADDR(clst, disk) + off / BYTES_PER_SECTOR;
    u8 *sector = kmalloc(BYTES_PER_SECTOR);
    if (!sector)
        return -ENOMEM;

    int err = gd->ops->disk_read(gd, lba, sector, 1);
    if (err >= 0) {
        struct fat_file *e = (struct fat_file*)(sector + off % BYTES_PER_SECTOR);
        e->file_size = fi->entry.file_size;
        e->cl_low = fi->entry.cl_low;
        e->cl_high = fi->entry.cl_high;
        e->last_write_date = fi->entry.last_write_date;
        e->last_write_time = fi->entry.last_write_time;
        e->last_access_date = fi->entry.last_access_date;
        err = gd->ops->disk_write(gd, lba, sector, 1);
    }
    kfree(sector);
    return err < 0 ? err : 0;
}

u32 __fat_get_clst_num(struct file *file, struct fat_disk *disk)
{
    struct fat_file *fat_file = (struct fat_file*)file->f_dentry->d_inode->i_private;
    u32 clst_num = fat_clst_value(fat_file);
    u32 clst_off = file->f_pos / disk->bytes_per_clst;

    while (clst_off--) {
        if (clst_num > 0x0FFFFFF8)
            return 0;
        clst_num = fat_value(clst_num, disk);
    }

    return clst_num;
}

u32 __fat_find_free_clst(struct fat_disk *disk)
{
    const u32 last = disk->FAT.last_clst;
    u32 start = disk->fs_info.next_free_clst;

    if (disk->fs_info.free_clst_cnt == 0)
        return 0;
    if (start < 2 || start > last)
        start = 2;

    u32 clst = start;
    do {
        if (fat_value(clst, disk) == 0)
            return clst;
        clst = clst == last ? 2 : clst + 1;
    } while (clst != start);

    return 0;
}

u32 __fat_add_new_clst(struct fat_disk *disk, u32 prev_clst, u32 new_clst)
{
    if (prev_clst != 0)
        fat_set_value(prev_clst, new_clst, disk);
    fat_set_value(new_clst, 0x0FFFFFFF, disk); // Mark new clst as EOF

    disk->fs_info.free_clst_cnt--;
    disk->fs_info.next_free_clst = __fat_find_free_clst(disk);
    return new_clst;
}

u32 __fat_find_alloc_clst(struct fat_disk *disk, u32 prev_clst)
{
    int new_clst = __fat_find_free_clst(disk);
    if (new_clst <= 0)
        return 0;
    return __fat_add_new_clst(disk, prev_clst, new_clst);
}

void fat_free_chain(struct fat_disk *disk, u32 clst)
{
    while (clst >= 2 && clst < 0x0FFFFFF8) {
        u32 next = fat_value(clst, disk);
        fat_set_value(clst, 0, disk);
        if (disk->fs_info.free_clst_cnt != 0xFFFFFFFF)
            disk->fs_info.free_clst_cnt++;
        if (clst < disk->fs_info.next_free_clst)
            disk->fs_info.next_free_clst = clst;
        clst = next;
    }
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
    root_inode->i_nlink = 1;
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
    mutex_init(&root_dentry->d_lock);

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

#ifdef DEBUG_FAT
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
#endif
