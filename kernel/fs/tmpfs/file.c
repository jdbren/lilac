#include <lilac/fs.h>
#include <lilac/log.h>
#include <lilac/libc.h>
#include <lilac/timer.h>
#include <lilac/err.h>
#include <mm/kmalloc.h>

#include "tmpfs_internal.h"


static int tmpfs_close(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t tmpfs_read(struct file *file, void *buf, size_t cnt)
{
    struct inode *inode = file->f_dentry->d_inode;
    struct tmpfs_file *tmp_inode = (struct tmpfs_file*)inode->i_private;
    size_t read = 0;

    if (file->f_pos >= inode->i_size)
        return 0;

    if (file->f_pos + cnt > inode->i_size)
        cnt = inode->i_size - file->f_pos;

    unsigned char *data = tmp_inode->data + file->f_pos;
    while (cnt--) {
        *(char*)buf++ = *data++;
        read++;
    }

    return read;
}

static ssize_t tmpfs_write(struct file *file, const void *buf, size_t cnt)
{
    struct inode *inode = file->f_dentry->d_inode;
    struct tmpfs_file *tmp_inode = (struct tmpfs_file*)inode->i_private;
    size_t written = 0;

    if (file->f_pos + cnt > inode->i_size) {
        void *data = krealloc(tmp_inode->data, file->f_pos + cnt);
        if (!data)
            return -ENOMEM;
        tmp_inode->data = data;
        if (file->f_pos > inode->i_size)
            memset((char*)data + inode->i_size, 0, file->f_pos - inode->i_size);
        inode->i_size = file->f_pos + cnt;
    }

    memcpy(tmp_inode->data + file->f_pos, buf, cnt);

    written = cnt;

    return written;
}

static
int tmpfs_readdir(struct file *file, struct dirent *dirp, unsigned int count)
{
    struct inode *inode = file->f_dentry->d_inode;
    struct tmpfs_dir *dir = (struct tmpfs_dir*)inode->i_private;
    size_t pos = file->f_pos;
    u32 i = 0;

    for (; i < count && pos < 2; i++, pos++) {
        struct dentry *d = pos == 0 ? file->f_dentry
            : (file->f_dentry->d_parent ? file->f_dentry->d_parent : file->f_dentry);
        strcpy(dirp[i].d_name, pos == 0 ? "." : "..");
        dirp[i].d_ino = d->d_inode ? d->d_inode->i_ino : inode->i_ino;
        dirp[i].d_off = pos;
        dirp[i].d_reclen = sizeof(struct dirent);
        dirp[i].d_type = DT_DIR;
        dirp[i].pad = 0;
    }

    struct tmpfs_entry *entry = pos - 2 < dir->num_entries ? dir->children + (pos - 2) : NULL;
#ifdef DEBUG_TMPFS
    klog(LOG_DEBUG, "tmpfs_readdir: inode = %p, dir = %p\n", inode, dir);
#endif
    while (i < count && pos - 2 < dir->num_entries) {
#ifdef DEBUG_TMPFS
        klog(LOG_DEBUG, "tmpfs_readdir: reading entry %u/%lu: name=%s\n",
            pos, dir->num_entries, entry->name);
#endif
        if (entry->inode) {
            strncpy(dirp[i].d_name, entry->name, sizeof(dirp[i].d_name));
            dirp[i].d_ino = entry->inode->i_ino;
            dirp[i].d_off = pos;
            dirp[i].d_reclen = sizeof(struct dirent);
            dirp[i].d_type = S_ISDIR(entry->inode->i_mode) ? DT_DIR
                : S_ISLNK(entry->inode->i_mode) ? DT_LNK : DT_REG;
            dirp[i].pad = 0;
            ++i;
        }
        ++pos;
        entry++;
    }

    file->f_pos = pos;
    return i;
}

const struct file_operations tmpfs_fops = {
    .read = tmpfs_read,
    .write = tmpfs_write,
    .readdir = tmpfs_readdir,
    .release = tmpfs_close
};
