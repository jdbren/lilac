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
    struct inode *inode = dentry->d_inode;

    if (--file->f_count)
        return;

    file->f_op->release(inode, file);
    kfree(file);

    dput(dentry);
}
