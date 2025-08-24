#include <fs/fat32.h>

#include <lilac/fs.h>
#include <lilac/log.h>
#include <lilac/libc.h>
#include <lilac/err.h>
#include <drivers/blkdev.h>
#include <lilac/kmalloc.h>

#include "fat_internal.h"

#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"

static inline bool check_entry(struct fat_file *entry, const char *name)
{
    if (entry->attributes != LONG_FNAME && entry->name[0] != FAT_UNUSED) {
        if (!memcmp(entry->name, name, 11))
            return true;
    }
    return false;
}

struct inode *fat_alloc_inode(struct super_block *sb)
{
    struct inode *new_node = kzmalloc(sizeof(struct inode));
    if (!new_node) {
        klog(LOG_ERROR, "fat_alloc_inode: Out of memory allocating inode\n");
        return ERR_PTR(-ENOMEM);
    }

    new_node->i_sb = sb;
    new_node->i_op = &fat_iops;
    new_node->i_count = 1;
    new_node->i_private = kzmalloc(sizeof(struct fat_inode));

    return new_node;
}

void fat_destroy_inode(struct inode *inode)
{
    if (inode->i_private)
        kfree(inode->i_private);
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

struct inode *fat_build_inode(struct super_block *sb, struct fat_inode *info)
{
    struct inode *inode;
    unsigned long pos = (u32)info->entry.cl_low + (u32)info->entry.cl_high;

    inode = fat_iget(sb, pos);
    if (inode) {
        klog(LOG_WARN, "Inode already exists\n");
        return inode;
    }

    inode = fat_alloc_inode(sb);
    if (IS_ERR_OR_NULL(inode)) {
        return inode;
    }

    inode->i_ino = unique_ino();
    inode->i_size = info->entry.file_size;
    inode->i_private = info;
    inode->i_type = info->entry.attributes & FAT_DIR_ATTR ? TYPE_DIR : TYPE_FILE;

    list_add_tail(&inode->i_list, &sb->s_inodes);

    return inode;
}

// Read a directory and return a buffer containing the directory entries
static void *__fat32_read_dir(struct fat_file *entry, struct fat_disk *disk,
    struct gendisk *hd)
{
    size_t bytes_read = 0;
    u32 clst = (u32)entry->cl_low + (u32)entry->cl_high;
    volatile unsigned char *current;
    volatile unsigned char *buffer = NULL;

    while (clst < 0x0FFFFFF8) {
        buffer = krealloc((void*)buffer, bytes_read + disk->bytes_per_clst);
        current = buffer + bytes_read;
        __fat_read_clst(disk, hd, clst, (void*)current);
        clst = __get_FAT_val(clst, disk);
        bytes_read += disk->bytes_per_clst;
    }

    return (void*)buffer;
}

static int fat32_find(struct inode *dir, const char *name,
    struct fat_inode *info)
{
    int ret = -1;
    struct fat_file *entry = (struct fat_file*)dir->i_private;
    struct fat_disk *disk = (struct fat_disk*)dir->i_sb->private;
    volatile unsigned char *buffer;

    buffer = __fat32_read_dir(entry, disk, dir->i_sb->s_bdev->disk);
    if (!buffer) {
        klog(LOG_ERROR, "Failed to read directory entries\n");
        return ret;
    }
    entry = (struct fat_file*)buffer;

    while (entry->name[0]) {
        if (check_entry(entry, name)) {
            memcpy(&info->entry, entry, sizeof(info->entry));
            ret = 0;
            break;
        }
        entry++;
    }

    kfree((void*)buffer);
    return ret;
}

struct dentry *fat32_lookup(struct inode *parent, struct dentry *find,
    unsigned int flags)
{
    struct inode *inode;
    struct fat_inode *info = kzmalloc(sizeof(*info));
    char fatname[12];
    const char *dot;
    int i, j;

    memset(fatname, ' ', 11);
    fatname[11] = '\0';

    dot = strchr(find->d_name, '.');

    // Copy up to 8 characters for the base name, stop at a dot
    for (i = 0, j = 0; i < 8 && find->d_name[j] != '\0' &&
         find->d_name[j] != '.'; i++, j++) {
        fatname[i] = toupper(find->d_name[j]);
    }

    if (dot) {
        for (i = 8, j = 1; j <= 3 && dot[j] != '\0'; i++, j++) {
            fatname[i] = toupper(dot[j]);
        }
    }

    if (fat32_find(parent, fatname, info) == 0) {
        inode = fat_build_inode(parent->i_sb, info);
        if (IS_ERR_OR_NULL(inode)) {
            kfree(info);
            return ERR_CAST(inode);
        }
        iget(inode);
        find->d_inode = inode;
        int i;
        for (i = 0; find->d_name[i]; i++) {
            find->d_name[i] = toupper(find->d_name[i]);
        }
        find->d_name[i] = '\0';
    }

    return NULL;
}
