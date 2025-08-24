#include <fs/stat.h>

#include <lilac/fs.h>
#include <lilac/syscall.h>
#include <lilac/err.h>
#include <lilac/log.h>
#include <lilac/kmalloc.h>
#include <lilac/uaccess.h>
#include <drivers/blkdev.h>

int vfs_stat(const struct file *f, struct stat *st)
{
    struct inode *i_ptr = f->f_dentry->d_inode;
    st->st_ino = i_ptr->i_ino;
    st->st_dev = i_ptr->i_sb->s_bdev->devnum;
    st->st_mode = i_ptr->i_mode;
    st->st_nlink = 0;
    st->st_uid = 0;
    st->st_gid = 0;
    if (i_ptr->i_type == TYPE_DEV)
        st->st_rdev = i_ptr->i_rdev;
    else
        st->st_rdev = 0;
    st->st_size = i_ptr->i_size;
    st->st_size = i_ptr->i_sb->s_blocksize;
    st->st_blocks = i_ptr->i_blocks;
    st->st_atime = i_ptr->i_atime;
    st->st_mtime = i_ptr->i_mtime;
    st->st_ctime = i_ptr->i_ctime;
    return 0;
}

SYSCALL_DECL2(stat, const char*, path, struct stat*, buf)
{
    struct file *file;
    char *path_buf = kmalloc(256);
    long err;

    err = strncpy_from_user(path_buf, path, 255);
    if (err < 0) {
        kfree(path_buf);
        return err;
    } else if (err == 0) {
        kfree(path_buf);
        return -ENAMETOOLONG;
    }

    // Get the file handle
    file = vfs_open(path_buf, 0, 0);
    if (IS_ERR(file))
        return PTR_ERR(file);

    // Get the stat structure
    err = vfs_stat(file, buf);
    if (err < 0) {
        vfs_close(file);
        return err;
    }

    vfs_close(file);
    kfree(path_buf);
    return 0;
}
