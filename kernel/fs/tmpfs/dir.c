#include <lilac/fs.h>
#include <lilac/kmalloc.h>
#include <lilac/libc.h>
#include <lilac/err.h>
#include "tmpfs_internal.h"
#include <lilac/log.h>

struct dentry *tmpfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
    struct tmpfs_dir *parent = (struct tmpfs_dir*)dir->i_private;
    struct tmpfs_entry *entry = (struct tmpfs_entry*)parent->children;
    size_t i;

    for (i = 0; i < parent->num_entries; i++) {
        if (!strcmp(entry->name, dentry->d_name)) {
            iget(entry->inode);
            dentry->d_inode = entry->inode;
            return dentry;
        }
        entry++;
    }

    return NULL;
}

int tmpfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
    struct inode *new_inode;
    struct tmpfs_dir *parent = (struct tmpfs_dir*)dir->i_private;
    struct tmpfs_dir *new_dir = kzmalloc(sizeof(struct tmpfs_dir));
    if (!new_dir) {
        klog(LOG_ERROR, "tmpfs_mkdir: Out of memory allocating tmpfs_dir\n");
        return -ENOMEM;
    }

    new_dir->num_entries = 0;
    new_dir->children = kzmalloc(4096);

    new_inode = dir->i_sb->s_op->alloc_inode(dir->i_sb);
    new_inode->i_private = new_dir;
    new_inode->i_mode = mode | S_IFDIR;
    dentry->d_inode = new_inode;

    parent->num_entries++;
    parent->children = krealloc(parent->children, parent->num_entries * sizeof(struct tmpfs_entry));

    struct tmpfs_entry *new_entry = parent->children + parent->num_entries - 1;
    new_entry->inode = new_inode;
    strcpy(new_entry->name, dentry->d_name);
    new_entry->type = TMPFS_DIR;

    return 0;
}

int tmpfs_readdir(struct file *file, struct dirent *dirp, unsigned int count)
{
    struct inode *inode = file->f_dentry->d_inode;
    struct tmpfs_dir *dir = (struct tmpfs_dir*)inode->i_private;
    struct tmpfs_entry *entry = dir->children;
    size_t pos = file->f_pos;
    u32 i = 0;
#ifdef DEBUG_TMPFS
    klog(LOG_DEBUG, "tmpfs_readdir: inode = %p, dir = %p\n", inode, dir);
#endif
    while (i < count && pos < dir->num_entries) {
        if (entry->inode) {
            strcpy(dirp[pos].d_name, entry->name);
            dirp[pos].d_ino = entry->inode->i_ino;
            dirp[pos].d_reclen = sizeof(struct dirent);
            ++pos;
            ++i;
        }
        entry++;
    }

    return i;
}
