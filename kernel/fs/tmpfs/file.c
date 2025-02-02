#include <lilac/fs.h>
#include <lilac/log.h>
#include <mm/kmalloc.h>

#include "tmpfs_internal.h"

#include <string.h>


int tmpfs_create(struct inode *parent, struct dentry *new_dentry, umode_t mode)
{
    struct inode *new_inode = parent->i_sb->s_op->alloc_inode(parent->i_sb);
    struct tmpfs_file *file_info = kzmalloc(sizeof(*file_info));

    new_inode->i_private = file_info;
    new_inode->i_mode = mode;
    new_inode->i_type = TYPE_FILE;
    file_info->data = kmalloc(4096);
    new_inode->i_size = 4096;
    new_dentry->d_inode = new_inode;

    struct tmpfs_dir *parent_dir = (struct tmpfs_dir*)parent->i_private;
    parent_dir->num_entries++;
    parent_dir->children = krealloc(parent_dir->children, parent_dir->num_entries * sizeof(struct tmpfs_entry));

    struct tmpfs_entry *new_entry = parent_dir->children + parent_dir->num_entries - 1;
    memset(new_entry, 0, sizeof(*new_entry));
    new_entry->inode = new_inode;
    strcpy(new_entry->name, new_dentry->d_name);
    new_entry->type = TMPFS_FILE;

    return 0;
}

int tmpfs_open(struct inode *inode, struct file *file)
{
    file->f_dentry->d_inode = inode;
    file->f_op = &tmpfs_fops;
    return 0;
}

int tmpfs_close(struct inode *inode, struct file *file)
{
    return 0;
}

ssize_t tmpfs_read(struct file *file, void *buf, size_t cnt)
{
    struct inode *inode = file->f_dentry->d_inode;
    struct tmpfs_file *tmp_inode = (struct tmpfs_file*)inode->i_private;
    size_t read = 0;

    if (file->f_pos + cnt > inode->i_size)
        cnt = inode->i_size - file->f_pos;

    unsigned char *data = tmp_inode->data + file->f_pos;
    while (cnt-- && *data) {
        *(char*)buf++ = *data++;
        read++;
    }

    return read;
}

ssize_t tmpfs_write(struct file *file, const void *buf, size_t cnt)
{
    struct inode *inode = file->f_dentry->d_inode;
    struct tmpfs_file *tmp_inode = (struct tmpfs_file*)inode->i_private;
    size_t written = 0;

    if (file->f_pos + cnt > inode->i_size) {
        tmp_inode->data = krealloc(tmp_inode->data, file->f_pos + cnt);
        inode->i_size = file->f_pos + cnt;
    }

    memcpy(tmp_inode->data + file->f_pos, buf, cnt);

    written = cnt;

    return written;
}
