#include <fs/fat32.h>

#include <lilac/fs.h>
#include <lilac/log.h>
#include <lilac/libc.h>
#include <lilac/err.h>
#include <lilac/time.h>
#include <drivers/blkdev.h>
#include <mm/kmalloc.h>

#include "fat_internal.h"

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
    new_node->i_mode = S_IEXEC|S_IWRITE|S_IREAD;

    return new_node;
}

void fat_destroy_inode(struct inode *inode)
{
    if (inode->i_private)
        kfree(inode->i_private);
    if (inode->i_list.next)
        list_del(&inode->i_list);
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
        i_pos = (u32)info->cl_low + ((u32)info->cl_high << 16);
        if (i_pos == pos)
            return tmp;
    }

    return NULL;
}

struct inode *fat_build_inode(struct super_block *sb, struct fat_inode *info)
{
    struct inode *inode;
    unsigned long pos = (u32)info->entry.cl_low + ((u32)info->entry.cl_high << 16);

    inode = fat_iget(sb, pos);
    if (inode) {
        klog(LOG_WARN, "Inode already exists\n");
        return inode;
    }

    inode = fat_alloc_inode(sb);
    if (IS_ERR(inode)) {
        return inode;
    }

    inode->i_ino = unique_ino();
    inode->i_size = info->entry.file_size;
    inode->i_atime = fat_time_to_unix(info->entry.last_access_date, 0);
    inode->i_mtime = fat_time_to_unix(info->entry.last_write_date, info->entry.last_write_time);
    inode->i_ctime = fat_time_to_unix(info->entry.creation_date, info->entry.creation_time);
    inode->i_private = info;
    inode->i_mode |= info->entry.attributes & FAT_DIR_ATTR ? S_IFDIR : S_IFREG;

    list_add_tail(&inode->i_list, &sb->s_inodes);

    return inode;
}

// Return 0 if found, 1 if not found, negative on error
static int fat32_find(struct inode *dir, const char *name,
    struct fat_inode *info)
{
    int ret = 1;
    int bytes = 0;
    struct fat_file *entry;
    struct fat_disk *disk = (struct fat_disk*)dir->i_sb->s_fs_info;
    volatile u8 *buffer = NULL;
    char lfn[256];
    char sfn[13];

    memset(lfn, 0, sizeof(lfn));

    struct fat_file *dir_entry = (struct fat_file*)dir->i_private;
    bytes = __fat32_read_dir(disk, &buffer, fat_clst_value(dir_entry));
    if (bytes <= 0) {
        klog(LOG_ERROR, "Failed to read directory entries\n");
        return bytes;
    }
    entry = (struct fat_file*)buffer;

    while (entry < (struct fat_file*)(buffer + bytes)) {
        if (entry->name[0] == 0x00) {
            break;
        }

        if (entry->attributes == LONG_FNAME) {
            fat_get_lfn_part(entry, lfn);
        } else if (entry->name[0] != FAT_UNUSED) {
            // Check LFN
            if (lfn[0] != 0) {
                if (!fat_strcasecmp(lfn, name)) {
                    memcpy(&info->entry, entry, sizeof(info->entry));
                    ret = 0;
                    break;
                }
            }

            // Check SFN
            fat_get_sfn(entry, sfn);
            if (!fat_strcasecmp(sfn, name)) {
                memcpy(&info->entry, entry, sizeof(info->entry));
                ret = 0;
                break;
            }

            memset(lfn, 0, sizeof(lfn));
        } else {
            memset(lfn, 0, sizeof(lfn));
        }
        entry++;
    }

    kfree((void*)buffer);
    return ret;
}

#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"

struct dentry *fat32_lookup(struct inode *parent, struct dentry *find,
    unsigned int flags)
{
    struct inode *inode;
    struct fat_inode *info = kzmalloc(sizeof(*info));
    long err = 0;

    if (!info)
        return ERR_PTR(-ENOMEM);

    if ((err = fat32_find(parent, find->d_name, info)) == 0) {
        inode = fat_build_inode(parent->i_sb, info);
        if (IS_ERR(inode)) {
            kfree(info);
            return ERR_CAST(inode);
        }
        iget(inode);
        find->d_inode = inode;
    } else if (err > 0) {
        kfree(info);
    } else {
        kfree(info);
        return ERR_PTR(err);
    }

    return NULL;
}

#pragma GCC diagnostic pop
