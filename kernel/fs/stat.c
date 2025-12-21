#include <fs/stat.h>

#include <lilac/fs.h>
#include <lilac/syscall.h>
#include <lilac/err.h>
#include <lilac/log.h>
#include <mm/kmalloc.h>
#include <lilac/uaccess.h>
#include <drivers/blkdev.h>

int vfs_stat(const struct file *f, struct stat *st)
{
    struct inode *i_ptr;
    if (f->f_inode) {
        i_ptr = f->f_inode;
    } else if (f->f_dentry) {
        i_ptr = f->f_dentry->d_inode;
    } else {
        klog(LOG_WARN, "vfs_stat: unexpected missing inode on %p", f);
        return -EBADF;
    }

    st->st_ino = i_ptr->i_ino;
    // st->st_dev = i_ptr->i_sb->s_bdev->devnum;
    st->st_dev = 0;
    st->st_mode = i_ptr->i_mode;
    st->st_nlink = i_ptr->i_count;
    st->st_uid = 0;
    st->st_gid = 0;
    st->st_rdev = i_ptr->i_rdev;
    st->st_size = i_ptr->i_size;
    if (i_ptr->i_sb)
        st->st_blksize = i_ptr->i_sb->s_blocksize;
    st->st_blksize = 512;
    st->st_blocks = i_ptr->i_blocks;
    st->st_atim = (struct timespec){i_ptr->i_atime, 0};
    st->st_mtim = (struct timespec){i_ptr->i_mtime, 0};
    st->st_ctim = (struct timespec){i_ptr->i_ctime, 0};
    return 0;
}

SYSCALL_DECL2(stat, const char*, path, struct stat*, buf)
{
    struct file *file;
    char *path_buf;
    long err;

    path_buf = get_user_path(path);
    if (IS_ERR(path_buf))
        return PTR_ERR(path_buf);

    if (!access_ok(buf, sizeof(*buf)))
        return -EFAULT;

    file = vfs_open(path_buf, 0, 0);
    if (IS_ERR(file))
        return PTR_ERR(file);

    err = vfs_stat(file, buf);
    if (err < 0) {
        vfs_close(file);
        return err;
    }

    vfs_close(file);
    kfree(path_buf);
    return 0;
}

SYSCALL_DECL2(fstat, int, fd, struct stat*, buf)
{
    struct file *f = get_file_handle(fd);
    if (IS_ERR_OR_NULL(f))
        return -EBADF;
    if (!access_ok(buf, sizeof(struct stat)))
        return -EFAULT;
    return vfs_stat(f, buf);
}
