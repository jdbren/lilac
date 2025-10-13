#include <lilac/fs.h>
#include <lilac/err.h>
#include <lilac/kmalloc.h>

struct file * alloc_file(struct dentry *d)
{
    struct file *file = kzmalloc(sizeof(*file));
    if (!file)
        return NULL;

    spin_lock_init(&file->f_lock);
    mutex_init(&file->f_pos_lock);
    if (d)
        dget(d);
    file->f_dentry = d;
    return file;
}

void fget(struct file *file)
{
    file->f_count++;
}

void fput(struct file *file)
{
    struct dentry *dentry = file->f_dentry;
    struct inode *inode = NULL;
    if (dentry)
        inode = dentry->d_inode;

    if (--file->f_count)
        return;

    if (file->f_op->release)
        file->f_op->release(inode, file);
    kfree(file);

    if (dentry)
        dput(dentry);
}
