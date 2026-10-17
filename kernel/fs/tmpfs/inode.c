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
        if (!lstrcmp_cstr(&dentry->d_name, entry->name)) {
            iget(entry->inode);
            dentry->d_inode = entry->inode;
            return NULL;
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

static int tmpfs_dir_append(struct tmpfs_dir *dir, const struct tmpfs_entry *e);

static int tmpfs_add_entry(struct inode *dir, struct dentry *dentry,
    struct inode *inode, unsigned short type)
{
    struct tmpfs_entry entry = { .inode = inode, .type = type };
    strncpy(entry.name, dentry->d_name.data, sizeof(entry.name) - 1);
    return tmpfs_dir_append((struct tmpfs_dir*)dir->i_private, &entry);
}

static int tmpfs_instantiate(struct inode *dir, struct dentry *dentry,
    struct inode *inode, unsigned short type)
{
    int err = tmpfs_add_entry(dir, dentry, inode, type);
    if (err) {
        iput(inode);
        return err;
    }
    iget(inode);
    dentry->d_inode = inode;
    dir->i_mtime = dir->i_ctime = inode->i_ctime;
    return 0;
}

static int
tmpfs_create(struct inode *dir, struct dentry *new_dentry, umode_t mode)
{
    struct tmpfs_file *file_info = kzmalloc(sizeof(*file_info));
    if (!file_info)
        return -ENOMEM;
    file_info->data = kmalloc(4096);
    if (!file_info->data) {
        kfree(file_info);
        return -ENOMEM;
    }

    struct inode *inode = tmpfs_new_inode(dir->i_sb, mode | S_IFREG, file_info);
    if (!inode) {
        kfree(file_info->data);
        kfree(file_info);
        return -ENOMEM;
    }
    return tmpfs_instantiate(dir, new_dentry, inode, TMPFS_FILE);
}

static int tmpfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
    struct tmpfs_dir *new_dir = kzmalloc(sizeof(struct tmpfs_dir));
    if (!new_dir) {
        klog(LOG_ERROR, "tmpfs_mkdir: Out of memory allocating tmpfs_dir\n");
        return -ENOMEM;
    }

    struct inode *inode = tmpfs_new_inode(dir->i_sb, mode | S_IFDIR, new_dir);
    if (!inode) {
        kfree(new_dir);
        return -ENOMEM;
    }
    return tmpfs_instantiate(dir, dentry, inode, TMPFS_DIR);
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
        if (entry->inode == inode && strcmp(entry->name, dentry->d_name.data) == 0) {
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

    inode->i_nlink = 0;
    iput(inode);

    return 0;
}

static int
tmpfs_link(struct dentry *old_d, struct inode *dir, struct dentry *new_d)
{
    struct inode *old_inode = old_d->d_inode;
    if (S_ISDIR(old_inode->i_mode))
        return -EPERM;

    iget(old_inode); // the new entry
    int err = tmpfs_add_entry(dir, new_d, old_inode,
        S_ISLNK(old_inode->i_mode) ? TMPFS_SYMLINK : TMPFS_FILE);
    if (err) {
        iput(old_inode);
        return err;
    }
    iget(old_inode); // the new dentry
    old_inode->i_nlink++;
    new_d->d_inode = old_inode;
    old_inode->i_ctime = dir->i_mtime = dir->i_ctime = get_unix_time();

    return 0;
}

static int tmpfs_unlink(struct inode *dir, struct dentry *dentry)
{
    struct inode *inode = dentry->d_inode;
    struct tmpfs_dir *dir_info = (struct tmpfs_dir*)dir->i_private;

    for (unsigned long i = 0; i < dir_info->num_entries; i++) {
        struct tmpfs_entry *entry = &dir_info->children[i];
        if (entry->inode == inode && lstrcmp_cstr(&dentry->d_name, entry->name) == 0) {
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
    iput(inode);

    return 0;
}

static int
tmpfs_symlink(struct inode *dir, struct dentry *link_d, const char *target)
{
    size_t target_len = strlen(target);
    struct tmpfs_file *file_info = kzmalloc(sizeof(*file_info));
    if (!file_info)
        return -ENOMEM;
    file_info->data = kmalloc(target_len + 1);
    if (!file_info->data) {
        kfree(file_info);
        return -ENOMEM;
    }
    memcpy(file_info->data, target, target_len + 1);

    struct inode *inode = tmpfs_new_inode(dir->i_sb, S_IFLNK | 0777, file_info);
    if (!inode) {
        kfree(file_info->data);
        kfree(file_info);
        return -ENOMEM;
    }
    inode->i_size = target_len;
    return tmpfs_instantiate(dir, link_d, inode, TMPFS_SYMLINK);
}

static int tmpfs_readlink(struct dentry *dentry, char __user *buf, int bufsize)
{
    struct inode *inode = dentry->d_inode;
    struct tmpfs_file *tmp_inode = (struct tmpfs_file*)inode->i_private;

    if (bufsize <= 0)
        return -EINVAL;
    int len = MIN((int)inode->i_size, bufsize);

    return copy_to_user(buf, tmp_inode->data, len) ? -EFAULT : len;
}

static long tmpfs_dir_find(struct tmpfs_dir *dir, const char *name, struct inode *inode)
{
    for (unsigned long i = 0; i < dir->num_entries; i++) {
        if (dir->children[i].inode == inode && strcmp(dir->children[i].name, name) == 0)
            return i;
    }
    return -1;
}

static void tmpfs_dir_remove(struct tmpfs_dir *dir, unsigned long idx)
{
    for (unsigned long j = idx; j + 1 < dir->num_entries; j++)
        dir->children[j] = dir->children[j + 1];
    dir->num_entries--;
}

static int tmpfs_dir_append(struct tmpfs_dir *dir, const struct tmpfs_entry *e)
{
    struct tmpfs_entry *c = krealloc(dir->children,
        (dir->num_entries + 1) * sizeof(struct tmpfs_entry));
    if (!c)
        return -ENOMEM;
    dir->children = c;
    dir->children[dir->num_entries++] = *e;
    return 0;
}

static int tmpfs_rename(struct inode *old_dir, struct dentry *old_d,
                        struct inode *new_dir, struct dentry *new_d)
{
    struct tmpfs_dir *od = (struct tmpfs_dir*)old_dir->i_private;
    struct tmpfs_dir *nd = (struct tmpfs_dir*)new_dir->i_private;
    struct inode *inode = old_d->d_inode;
    struct inode *target = new_d->d_inode;

    long idx = tmpfs_dir_find(od, old_d->d_name.data, inode);
    if (idx < 0)
        return -ENOENT;
    struct tmpfs_entry moved = od->children[idx];

    if (target) {
        if (S_ISDIR(target->i_mode) &&
                ((struct tmpfs_dir*)target->i_private)->num_entries > 0)
            return -ENOTEMPTY;
        long tidx = tmpfs_dir_find(nd, new_d->d_name.data, target);
        if (tidx >= 0) {
            tmpfs_dir_remove(nd, tidx);
            target->i_nlink--;
            iput(target);
        }
        // the old entry may have shifted if both live in the same directory
        idx = tmpfs_dir_find(od, old_d->d_name.data, inode);
        if (idx < 0)
            return -ENOENT;
    }

    tmpfs_dir_remove(od, idx);
    memset(moved.name, 0, sizeof(moved.name));
    strncpy(moved.name, new_d->d_name.data, sizeof(moved.name) - 1);
    return tmpfs_dir_append(nd, &moved);
}

// data always holds at least i_size bytes; bytes past i_size may be stale
static int tmpfs_truncate(struct inode *inode, loff_t size)
{
    struct tmpfs_file *file = (struct tmpfs_file*)inode->i_private;
    if (size > inode->i_size) {
        char *data = krealloc(file->data, size);
        if (!data)
            return -ENOMEM;
        memset(data + inode->i_size, 0, size - inode->i_size);
        file->data = data;
    }
    inode->i_size = size;
    return 0;
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
    .readlink = tmpfs_readlink,
    .truncate = tmpfs_truncate,
    .rename = tmpfs_rename,
};
