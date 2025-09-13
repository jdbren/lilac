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
    if (i_ptr->i_type == TYPE_DEV) {
        st->st_rdev = i_ptr->i_rdev;
        st->st_mode |= S_IFCHR;
    } else {
        st->st_rdev = 0;
    }
    if (i_ptr->i_type == TYPE_DIR)
        st->st_mode |= S_IFDIR;
    if (i_ptr->i_type == TYPE_FILE)
        st->st_mode |= S_IFREG;
    st->st_size = i_ptr->i_size;
    st->st_blksize = i_ptr->i_sb->s_blocksize;
    st->st_blocks = i_ptr->i_blocks;
    st->st_atim = (struct timespec){i_ptr->i_atime, 0};
    st->st_mtim = (struct timespec){i_ptr->i_mtime, 0};
    st->st_ctim = (struct timespec){i_ptr->i_ctime, 0};
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

    if (!access_ok(buf, sizeof(struct stat)))
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
