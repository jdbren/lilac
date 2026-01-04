#include <lilac/fs.h>
#include <lilac/log.h>
#include <lilac/libc.h>
#include <lilac/timer.h>
#include <lilac/err.h>
#include <lilac/uaccess.h>
#include <mm/kmalloc.h>

#include "tmpfs_internal.h"

static int tmpfs_open(struct inode *inode, struct file *file)
{
    file->f_op = &tmpfs_fops;
    return 0;
}

static struct dentry *tmpfs_lookup(struct inode *dir, struct dentry *dentry,
    unsigned int flags)
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

static const char *
tmpfs_get_link(struct dentry *resolve_d, struct inode *sym_i)
{
    struct tmpfs_file *tmp_inode = (struct tmpfs_file*)sym_i->i_private;
    return (const char*)tmp_inode->data;
}

static int
tmpfs_create(struct inode *parent, struct dentry *new_dentry, umode_t mode)
{
    struct inode *new_inode = parent->i_sb->s_op->alloc_inode(parent->i_sb);
    struct tmpfs_file *file_info = kzmalloc(sizeof(*file_info));

    new_inode->i_private = file_info;
    new_inode->i_mode = mode | S_IFREG;
    file_info->data = kmalloc(4096);
    new_inode->i_size = 0;
    new_inode->i_count = 1;
    new_inode->i_ctime = new_inode->i_mtime = new_inode->i_atime = get_unix_time();
    new_dentry->d_inode = new_inode;

    struct tmpfs_dir *parent_dir = (struct tmpfs_dir*)parent->i_private;
    parent_dir->num_entries++;
    parent_dir->children = krealloc(parent_dir->children,
        parent_dir->num_entries * sizeof(struct tmpfs_entry));

    struct tmpfs_entry *new_entry = parent_dir->children +
        parent_dir->num_entries - 1;
    memset(new_entry, 0, sizeof(*new_entry));
    new_entry->inode = new_inode;
    strcpy(new_entry->name, new_dentry->d_name);
    new_entry->type = TMPFS_FILE;

    return 0;
}

static int tmpfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
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
    new_inode->i_ctime = new_inode->i_mtime = new_inode->i_atime = get_unix_time();
    dentry->d_inode = new_inode;

    parent->num_entries++;
    parent->children = krealloc(parent->children,
        parent->num_entries * sizeof(struct tmpfs_entry));

    struct tmpfs_entry *new_entry = parent->children + parent->num_entries - 1;
    new_entry->inode = new_inode;
    strcpy(new_entry->name, dentry->d_name);
    new_entry->type = TMPFS_DIR;

    return 0;
}

static int tmpfs_rmdir(struct inode *dir, struct dentry *dentry)
{
    struct inode *inode = dentry->d_inode;
    struct tmpfs_dir *parent_dir = (struct tmpfs_dir*)dir->i_private;
    struct tmpfs_dir *target_dir = (struct tmpfs_dir*)inode->i_private;

    if (target_dir->num_entries > 0) {
        return -ENOTEMPTY;
    }

    for (unsigned long i = 0; i < parent_dir->num_entries; i++) {
        struct tmpfs_entry *entry = &parent_dir->children[i];
        if (entry->inode == inode && strcmp(entry->name, dentry->d_name) == 0) {
            // Remove entry by shifting others
            for (unsigned long j = i; j < parent_dir->num_entries - 1; j++) {
                parent_dir->children[j] = parent_dir->children[j + 1];
            }
            parent_dir->num_entries--;
            parent_dir->children = krealloc(parent_dir->children,
                parent_dir->num_entries * sizeof(struct tmpfs_entry));
            break;
        }
    }

    inode->i_nlink--;
    if (inode->i_nlink == 0) {
        inode->i_sb->s_op->destroy_inode(inode);
    }

    return 0;
}

static int
tmpfs_link(struct dentry *old_d, struct inode *dir, struct dentry *new_d)
{
    struct inode *old_inode = old_d->d_inode;
    if (S_ISDIR(old_inode->i_mode))
        return -EPERM;

    iget(old_inode);
    old_inode->i_nlink++;
    new_d->d_inode = old_inode;

    struct tmpfs_dir *dir_info = (struct tmpfs_dir*)dir->i_private;
    dir_info->num_entries++;
    dir_info->children = krealloc(dir_info->children,
        dir_info->num_entries * sizeof(struct tmpfs_entry));

    struct tmpfs_entry *new_entry = dir_info->children + dir_info->num_entries - 1;
    memset(new_entry, 0, sizeof(*new_entry));
    new_entry->inode = old_inode;
    strcpy(new_entry->name, new_d->d_name);
    new_entry->type = S_ISDIR(old_inode->i_mode) ? TMPFS_DIR : TMPFS_FILE;

    return 0;
}

static int tmpfs_unlink(struct inode *dir, struct dentry *dentry)
{
    struct inode *inode = dentry->d_inode;
    struct tmpfs_dir *dir_info = (struct tmpfs_dir*)dir->i_private;

    for (unsigned long i = 0; i < dir_info->num_entries; i++) {
        struct tmpfs_entry *entry = &dir_info->children[i];
        if (entry->inode == inode && strcmp(entry->name, dentry->d_name) == 0) {
            // Remove entry by shifting others
            for (unsigned long j = i; j < dir_info->num_entries - 1; j++) {
                dir_info->children[j] = dir_info->children[j + 1];
            }
            dir_info->num_entries--;
            dir_info->children = krealloc(dir_info->children,
                dir_info->num_entries * sizeof(struct tmpfs_entry));
            break;
        }
    }

    inode->i_nlink--;

    return 0;
}

static int
tmpfs_symlink(struct inode *dir, struct dentry *link_d, const char *target)
{
    struct inode *new_inode = dir->i_sb->s_op->alloc_inode(dir->i_sb);
    struct tmpfs_file *file_info = kzmalloc(sizeof(*file_info));

    new_inode->i_private = file_info;
    new_inode->i_mode = S_IFLNK | 0777;
    size_t target_len = strlen(target);
    file_info->data = kmalloc(target_len + 1);
    memcpy(file_info->data, target, target_len + 1);
    new_inode->i_size = target_len;
    new_inode->i_count = 1;
    new_inode->i_ctime = new_inode->i_mtime = new_inode->i_atime = get_unix_time();
    link_d->d_inode = new_inode;

    struct tmpfs_dir *parent_dir = (struct tmpfs_dir*)dir->i_private;
    parent_dir->num_entries++;
    parent_dir->children = krealloc(parent_dir->children,
        parent_dir->num_entries * sizeof(struct tmpfs_entry));

    struct tmpfs_entry *new_entry = parent_dir->children +
        parent_dir->num_entries - 1;
    memset(new_entry, 0, sizeof(*new_entry));
    new_entry->inode = new_inode;
    strcpy(new_entry->name, link_d->d_name);
    new_entry->type = TMPFS_SYMLINK;

    return 0;
}

static int tmpfs_readlink(struct dentry *dentry, char __user *buf, int bufsize)
{
    struct inode *inode = dentry->d_inode;
    struct tmpfs_file *tmp_inode = (struct tmpfs_file*)inode->i_private;

    if (bufsize < 0)
        return -EINVAL;
    if (bufsize < inode->i_size + 1)
        return -EFAULT;

    return copy_to_user(buf, tmp_inode->data, inode->i_size + 1) ?
              -EFAULT : inode->i_size;
}

const struct inode_operations tmpfs_iops = {
    .open = tmpfs_open,
    .lookup = tmpfs_lookup,
    .get_link = tmpfs_get_link,
    .create = tmpfs_create,
    .mkdir = tmpfs_mkdir,
    .rmdir = tmpfs_rmdir,
    .link = tmpfs_link,
    .unlink = tmpfs_unlink,
    .symlink = tmpfs_symlink,
    .readlink = tmpfs_readlink
};
