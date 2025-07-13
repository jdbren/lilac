#include <lilac/file.h>
#include <lilac/fs.h>
#include <mm/kmalloc.h>

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
