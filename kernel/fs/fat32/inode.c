#include <fs/fat32.h>

#include <lilac/fs.h>
#include <lilac/log.h>
#include <drivers/blkdev.h>
#include <mm/kmalloc.h>

#include <string.h>
#include <ctype.h>

#include "fat_internal.h"

static inline bool check_entry(struct fat_file *entry, const char *cur)
{
    if (entry->attributes != LONG_FNAME && entry->name[0] != (char)FAT_UNUSED) {
        if (!memcmp(entry->name, cur, strlen(cur)))
            return true;
    }
    return false;
}

inline struct inode *fat_alloc_inode(struct super_block *sb)
{
    struct inode *new_node = kzmalloc(sizeof(struct inode));

    new_node->i_sb = sb;
    new_node->i_op = &fat_iops;
    new_node->i_count = 1;

    return new_node;
}

inline void fat_destroy_inode(struct inode *inode)
{
    kfree(inode);
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

struct inode *fat_build_inode(struct super_block *sb, struct fat_file *info)
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

// Read a directory and return a buffer containing the directory entries
static void *__fat32_read_dir(struct fat_file *entry, struct fat_disk *disk,
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

static int fat32_find(struct inode *dir, const char *name,
    struct fat_file *info)
{
    int ret = -1;
    struct fat_file *entry = (struct fat_file*)dir->i_private;
    struct fat_disk *disk = (struct fat_disk*)dir->i_sb->private;
    volatile unsigned char *buffer;

    buffer = __fat32_read_dir(entry, disk, dir->i_sb->s_bdev->disk);
    entry = (struct fat_file*)buffer;

    while (entry->name[0]) {
        if (check_entry(entry, name)) {
            memcpy(info, entry, sizeof(*info));
            ret = 0;
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
