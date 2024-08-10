// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <fs/fat32.h>

#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include <lilac/types.h>
#include <lilac/panic.h>
#include <lilac/fs.h>
#include <lilac/file.h>
#include <lilac/list.h>
#include <drivers/blkdev.h>
#include <mm/kmm.h>
#include <mm/kmalloc.h>

#define LONG_FNAME 0x0F
#define FAT_DIR_ATTR 0x10
#define UNUSED 0xE5
#define MAX_SECTOR_READS 64
#define BYTES_PER_SECTOR 512
#define FAT_BUFFER_SIZE (BYTES_PER_SECTOR * MAX_SECTOR_READS)

#define ROUND_UP(x,bps)    ((((uintptr_t)(x)) + (u32)bps-1) & (~((u32)bps-1)))

#define FAT_SIGNATURE 0xAA55
#define FAT32_FS_INFO_SIG1 0x41615252
#define FAT32_FS_INFO_SIG2 0x61417272
#define FAT32_FS_INFO_TRAIL_SIG 0xAA550000

#define FAT_SET_DATE(year, month, day) \
    (((year - 1980) << 9) | (month << 5) | day)
#define FAT_SET_TIME(hour, min, sec) \
    ((hour << 11) | (min << 5) | (sec >> 1))

struct fat_FAT_buf {
    u32 first_clst;
    u32 last_clst;
    u32 sectors;
    volatile u32 buf[FAT_BUFFER_SIZE / 4];
};

struct fat_file_buf {
    u32 cl;
    u32 buf_sz;
    volatile u8 *buffer;
};

struct fat_disk {
    u32 base_lba;
    u32 fat_begin_lba;
    u32 clst_begin_lba;
    u32 root_start;
    u16 bytes_per_clst;
    u16 sect_per_clst;
    volatile struct fat_BS bpb;
    volatile struct FSInfo fs_info;
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

static struct inode *fat_build_inode(struct super_block *sb, struct fat_file *info);
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
    if (fat_disk->fs_info.lead_sig != FAT32_FS_INFO_SIG1 ||
        fat_disk->fs_info.struct_sig != FAT32_FS_INFO_SIG2 ||
        fat_disk->fs_info.trail_sig != FAT32_FS_INFO_TRAIL_SIG)
        return -1;
    return 0;
}

static inline int __must_check
fat32_write_fs_info(struct fat_disk *fat_disk, struct gendisk *gd)
{
    return gd->ops->disk_write(gd, fat_disk->base_lba +
        fat_disk->bpb.extended_section.fs_info, &fat_disk->fs_info, 1);
}

static int fat_read_FAT(struct fat_disk *fat_disk, struct gendisk *hd,
    u32 clst_off)
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

static int fat_write_FAT(struct fat_disk *fat_disk, struct gendisk *gd)
{
    const u32 lba = fat_disk->fat_begin_lba +
        ((fat_disk->FAT.first_clst - fat_disk->clst_begin_lba)
        * fat_disk->sect_per_clst);

    return gd->ops->disk_write(gd, lba, fat_disk->FAT.buf, fat_disk->FAT.sectors);
}

static u32 __get_FAT_val(u32 clst, struct fat_disk *disk)
{
    if (clst > disk->FAT.last_clst) {
        klog(LOG_DEBUG, "clst: %x\n", clst);
        klog(LOG_DEBUG, "last clst: %x\n", disk->FAT.last_clst);
        kerror("clst out of fat bounds\n");
    }
    return disk->FAT.buf[clst] & 0x0FFFFFFF;
}

static inline void __fat_read_clst(struct fat_disk *fat_disk,
    struct gendisk *hd, u32 clst, void *buf)
{
    hd->ops->disk_read(hd, LBA_ADDR(clst, fat_disk), buf,
        fat_disk->sect_per_clst);
}

static inline void __fat_write_clst(struct fat_disk *fat_disk,
    struct gendisk *hd, u32 clst, const void *buf)
{
    hd->ops->disk_write(hd, LBA_ADDR(clst, fat_disk), buf,
        fat_disk->sect_per_clst);
}

static u32 __get_clst_num(struct file *file, struct fat_disk *disk)
{
    struct fat_file *fat_file = (struct fat_file*)file->f_inode->i_private;
    u32 clst_num = (u32)fat_file->cl_low + ((u32)fat_file->cl_high << 16);
    u32 clst_off = file->f_pos / disk->bytes_per_clst;

    while (clst_off--) {
        if (clst_num > 0x0FFFFFF8)
            return 0;
        if (clst_num > disk->FAT.last_clst) {
            printf("clst: %x\n", clst_num);
            printf("last clst: %x\n", disk->FAT.last_clst);
            kerror("clst out of fat bounds\n");
        }
        clst_num = disk->FAT.buf[clst_num] & 0x0FFFFFFF;
    }

    return clst_num;
}

static u32 __find_free_clst(struct fat_disk *disk)
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
    printf("FSInfo:\n");
    printf("Lead sig: %x\n", fat_disk->fs_info.lead_sig);
    printf("Struct sig: %x\n", fat_disk->fs_info.struct_sig);
    printf("Free clst cnt: %x\n", fat_disk->fs_info.free_clst_cnt);
    printf("Next free clst: %x\n", fat_disk->fs_info.next_free_clst);
    printf("Trail sig: %x\n", fat_disk->fs_info.trail_sig);
#endif

    return root_dentry;

error:
    kfree(fat_disk);
    kfree(fat_file);
    return NULL;
}

// Read from a file into a buffer (always multiples of cluster size)
static int __do_fat32_read(const struct file *file, u32 clst, volatile u8 *buffer,
    size_t num_clst)
{
    if (num_clst == 0)
        return -1;

    size_t clst_read = 0;
    const struct inode *inode = file->f_inode;
    const struct gendisk *gd = inode->i_sb->s_bdev->disk;
    struct fat_disk *fat_disk = (struct fat_disk*)inode->i_sb->private;

    while (clst < 0x0FFFFFF8 && clst_read < num_clst) {
        __fat_read_clst(fat_disk, gd, clst, buffer);
        clst = __get_FAT_val(clst, fat_disk);
        buffer += fat_disk->bytes_per_clst;
        clst_read++;
    }

    return 0;
}

// Write to a file from a buffer (always multiples of cluster size)
static int __do_fat32_write(const struct file *file, u32 clst,
    const u8 *buffer, size_t num_clst)
{
    if (num_clst == 0)
        return -1;

    size_t clst_writ = 0;
    u32 next_val;
    const struct inode *inode = file->f_inode;
    const struct gendisk *gd = inode->i_sb->s_bdev->disk;
    struct fat_disk *fat_disk = (struct fat_disk*)inode->i_sb->private;

    while (clst_writ < num_clst) {
        __fat_write_clst(fat_disk, gd, clst, buffer);
        clst_writ++;

        clst = __get_FAT_val(clst, fat_disk);
        if (clst >= 0x0FFFFFF8 && clst_writ < num_clst) {
            next_val = __find_free_clst(fat_disk);
            if (next_val == -1)
                kerror("No free clusters\n");
            fat_disk->FAT.buf[clst] &= 0xF0000000;
            fat_disk->FAT.buf[clst] |= next_val & 0x0FFFFFFF;
            fat_disk->FAT.buf[next_val] |= 0x0FFFFFFF;
            clst = next_val;
        }

        buffer += fat_disk->bytes_per_clst;
    }

    return 0;
}

// Read a directory and return a buffer containing the directory entries
static void *fat32_read_dir(struct fat_file *entry, struct fat_disk *disk,
    struct gendisk *hd)
{
    size_t bytes_read = 0;
    u32 clst = (u32)entry->cl_low + (u32)entry->cl_high;
    unsigned char *current;
    volatile unsigned char *buffer = NULL;

    while (clst < 0x0FFFFFF8) {
        buffer = krealloc(buffer, bytes_read + disk->bytes_per_clst);
        current = buffer + bytes_read;
        __fat_read_clst(disk, hd, clst, current);
        clst = __get_FAT_val(clst, disk);
        bytes_read += disk->bytes_per_clst;
    }

    return buffer;
}

int fat32_open(struct inode *inode, struct file *file)
{
    klog(LOG_INFO, "Opening file %s\n", file->f_path);
    file->f_inode = inode;
    file->f_op = &fat_fops;
    file->f_count++;
    return 0;
}

ssize_t fat32_read(struct file *file, void *file_buf, size_t count)
{
    ssize_t bytes_read;
    u32 start_clst;
    struct fat_disk *disk = (struct fat_disk*)file->f_inode->i_sb->private;
    struct fat_file *fat_file = (struct fat_file*)file->f_inode->i_private;
    u32 offset = file->f_pos % disk->bytes_per_clst;
    u32 num_clst = ROUND_UP(count + offset, disk->bytes_per_clst) /
        disk->bytes_per_clst;
    volatile unsigned char *buffer = kvirtual_alloc(disk->bytes_per_clst * num_clst, PG_WRITE);

    start_clst = __get_clst_num(file, disk);
    if (start_clst == 0)
        goto out;

    bytes_read = __do_fat32_read(file, start_clst, buffer, num_clst);
    if (bytes_read < 0)
        goto out;

    if (file->f_pos + count > fat_file->file_size)
        count = fat_file->file_size - file->f_pos;
    memcpy(file_buf, buffer + offset, count);
    file->f_pos += count;
    bytes_read = count;

out:
    kvirtual_free(buffer, disk->bytes_per_clst * num_clst);
    return bytes_read;
}

ssize_t fat32_write(struct file *file, const void *file_buf, size_t count)
{
    ssize_t bytes_written;
    u32 start_clst;
    struct fat_disk *disk = (struct fat_disk*)file->f_inode->i_sb->private;
    struct fat_file *fat_file = (struct fat_file*)file->f_inode->i_private;
    u32 offset = file->f_pos % disk->bytes_per_clst;
    u32 num_clst = ROUND_UP(count + offset, disk->bytes_per_clst) /
        disk->bytes_per_clst;
    unsigned char *buffer = kvirtual_alloc(disk->bytes_per_clst * num_clst, PG_WRITE);

    start_clst = __get_clst_num(file, disk);
    if (start_clst == 0)
        goto out;

    if (offset) {
        if(__do_fat32_read(file, start_clst, buffer, 1) < 0)
            goto out;
    }

    if (file->f_pos + count > fat_file->file_size)
        fat_file->file_size = file->f_pos + count;
    memcpy(buffer + offset, file_buf, count);

    bytes_written = __do_fat32_write(file, start_clst, buffer, num_clst);
    if (bytes_written < 0)
        goto out;

    file->f_pos += count;
    bytes_written = count;

out:
    kvirtual_free(buffer, disk->bytes_per_clst * num_clst);
    return bytes_written;
}

// count is the size of the buffer
int fat32_readdir(struct file *file, struct dirent *dir_buf, unsigned int count)
{
    int num_dirents = count / sizeof(struct dirent);
    u32 start_clst;
    struct fat_disk *disk = (struct fat_disk*)file->f_inode->i_sb->private;
    u32 offset = file->f_pos % disk->bytes_per_clst;
    u32 num_clst = ROUND_UP(count + offset, disk->bytes_per_clst) /
        disk->bytes_per_clst;
    volatile unsigned char *buffer = kvirtual_alloc(disk->bytes_per_clst * num_clst, PG_WRITE);

    start_clst = __get_clst_num(file, disk);
    if (start_clst <= 0)
        return -1;
    if (__do_fat32_read(file, start_clst, buffer, num_clst) < 0)
        return -1;

    int i = 0;
    struct fat_file *entry = (struct fat_file*)buffer;
    while (i < num_dirents && entry->name[0] != 0) {
        strncpy(dir_buf->d_name, entry->name, 8);
        entry++;
        dir_buf++;
        i++;
    }

    kvirtual_free(buffer, disk->bytes_per_clst * num_clst);
    return i;
}

int fat32_mkdir(struct inode *dir, struct dentry *new, unsigned short mode)
{
    struct fat_file *entry;
    struct fat_disk *disk = (struct fat_disk*)dir->i_sb->private;
    struct gendisk *hd = dir->i_sb->s_bdev->disk;
    struct fat_file *parent_dir = (struct fat_file*)dir->i_private;
    u32 clst = parent_dir->cl_low + (u32)(parent_dir->cl_high << 16);
    volatile unsigned char *buffer = kzmalloc(disk->bytes_per_clst);
    char name[8];
    int ret = 0;

    memset(name, ' ', 8);
    for (int i = 0; i < 8 && isprint(new->d_name[i]); i++)
        name[i] = toupper(new->d_name[i]);

    while (clst < 0x0FFFFFF8) {
        __fat_read_clst(disk, hd, clst, buffer);
        for (entry = (struct fat_file*)buffer;
            !(entry->name[0] == 0 || entry->name[0] == 0xe5);
            entry++)
        {
            if (!strncmp(entry->name, name, 8)) {
                klog(LOG_INFO, "Directory %-8s already exists\n", name);
                ret = -1;
            }
        }

        if (entry->name[0] == 0 || entry->name[0] == 0xe5)
            break;

        clst = __get_FAT_val(clst, disk);
    }

    if (clst >= 0x0FFFFFF8)
        kerror("Need to allocate new cluster\n");

    u32 new_clst = __find_free_clst(disk);
    if (new_clst == -1)
        kerror("No free clusters\n");
    disk->FAT.buf[new_clst] |= 0x0fffffffUL;

    entry->attributes = FAT_DIR_ATTR;
    entry->cl_low = new_clst & 0xFFFF;
    entry->cl_high = new_clst >> 16;
    entry->file_size = 0;
    entry->creation_date = FAT_SET_DATE(1980, 1, 1);
    entry->creation_time = FAT_SET_TIME(0, 0, 0);
    entry->last_write_date = FAT_SET_DATE(1980, 1, 1);
    entry->last_write_time = FAT_SET_TIME(0, 0, 0);

    strncpy(entry->name, name, 8);
    strncpy(entry->ext, "   ", 3);

    __fat_write_clst(disk, hd, clst, buffer);

    new->d_inode = fat_build_inode(dir->i_sb, entry);

    memset(buffer, 0, disk->bytes_per_clst);
    entry = (struct fat_file*)buffer;
    entry->name[0] = '.';
    memset(entry->name + 1, ' ', 7 + 3);
    entry->file_size = 0;
    entry->attributes = FAT_DIR_ATTR;
    entry->cl_low = new_clst & 0xFFFF;
    entry->cl_high = new_clst >> 16;
    entry->creation_date = FAT_SET_DATE(1980, 1, 1);
    entry->creation_time = FAT_SET_TIME(0, 0, 0);
    entry->last_write_date = FAT_SET_DATE(1980, 1, 1);
    entry->last_write_time = FAT_SET_TIME(0, 0, 0);

    entry++;
    entry->name[0] = '.'; entry->name[1] = '.';
    memset(entry->name + 2, ' ', 6 + 3);
    entry->file_size = 0;
    entry->attributes = FAT_DIR_ATTR;
    entry->cl_low = clst & 0xFFFF;
    entry->cl_high = clst >> 16;
    entry->creation_date = FAT_SET_DATE(1980, 1, 1);
    entry->creation_time = FAT_SET_TIME(0, 0, 0);
    entry->last_write_date = FAT_SET_DATE(1980, 1, 1);
    entry->last_write_time = FAT_SET_TIME(0, 0, 0);

    disk->fs_info.free_clst_cnt--;
    disk->fs_info.next_free_clst = __find_free_clst(disk);
    __fat_write_clst(disk, hd, new_clst, buffer);
    fat_write_FAT(disk, hd);
    fat32_write_fs_info(disk, hd);

error:
    kfree(buffer);
    return ret;
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
        klog(LOG_WARN, "Inode already exists\n");
        kfree(info);
        return inode;
    }

    inode = fat_alloc_inode(sb);

    inode->i_ino = unique_ino();
    inode->i_size = info->file_size;
    inode->i_private = info;
    inode->i_type = info->attributes == FAT_DIR_ATTR ? TYPE_DIR : TYPE_FILE;

    list_add_tail(&inode->i_list, &sb->s_inodes);

    return inode;
}

static int fat32_find(struct inode *dir, const char *name,
    struct fat_file *info)
{
    int ret = 0;
    struct fat_file *entry = (struct fat_file*)dir->i_private;
    struct fat_disk *disk = (struct fat_disk*)dir->i_sb->private;
    volatile unsigned char *buffer;

    buffer = fat32_read_dir(entry, disk, dir->i_sb->s_bdev->disk);
    entry = (struct fat_file*)buffer;

    while (entry->name[0]) {
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
