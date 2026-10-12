#include <fs/stat.h>

#include <lilac/fs.h>
#include <lilac/syscall.h>
#include <lilac/err.h>
#include <lilac/log.h>
#include <mm/kmalloc.h>
#include <lilac/uaccess.h>
#include <lilac/time.h>
#include <lilac/timer.h>
#include <drivers/blkdev.h>

static int stat_inode(const struct inode *i_ptr, struct stat *st)
{
    st->st_ino = i_ptr->i_ino;
    st->st_dev = i_ptr->i_sb ? i_ptr->i_sb->s_dev : 0;
    st->st_mode = i_ptr->i_mode;
    st->st_nlink = i_ptr->i_nlink;
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
#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "vfs_stat: inode=%lu, size=%lu, mode=%o\n",
        st->st_ino, st->st_size, st->st_mode);
#endif
    return 0;
}

int vfs_stat(const struct file *f, struct stat *st)
{
    const struct inode *inode;
    if (f->f_inode)
        inode = f->f_inode;
    else if (f->f_dentry)
        inode = f->f_dentry->d_inode;
    else {
        klog(LOG_WARN, "vfs_stat: unexpected missing inode on %p", f);
        return -EBADF;
    }

    return stat_inode(inode, st);
}

static long stat_path(struct dentry *base, const char *path, struct stat *st,
    int follow_final)
{
    struct dentry *dentry = vfs_lookup_at(base, path, follow_final);
    if (IS_ERR(dentry))
        return PTR_ERR(dentry);
    long err = dentry->d_inode ? stat_inode(dentry->d_inode, st) : -ENOENT;
    dput(dentry);
    return err;
}

SYSCALL_DECL2(stat, const char*, path, struct stat*, buf)
{
    struct stat st;
    char *path_buf;
    long err;

    path_buf = get_user_path(path);
    if (IS_ERR(path_buf))
        return PTR_ERR(path_buf);

    if (!access_ok(buf, sizeof(*buf))) {
        kfree(path_buf);
        return -EFAULT;
    }

    err = stat_path(NULL, path_buf, &st, 1);
    kfree(path_buf);

    if (err < 0)
        return err;
    return copy_to_user(buf, &st, sizeof(st));
}

SYSCALL_DECL2(lstat, const char*, path, struct stat*, buf)
{
    struct stat st;
    char *path_buf = get_user_path(path);
    if (IS_ERR(path_buf))
        return PTR_ERR(path_buf);
    if (!access_ok(buf, sizeof(*buf))) {
        kfree(path_buf);
        return -EFAULT;
    }

    long err = stat_path(NULL, path_buf, &st, 0);
    kfree(path_buf);
    if (err < 0)
        return err;
    return copy_to_user(buf, &st, sizeof(st));
}

SYSCALL_DECL2(fstat, int, fd, struct stat*, buf)
{
    struct stat st;
    struct file *f = get_file_handle(fd);
    if (IS_ERR_OR_NULL(f))
        return -EBADF;
    if (!access_ok(buf, sizeof(struct stat)))
        return -EFAULT;
    int err = vfs_stat(f, &st);
    if (err < 0)
        return err;
    return copy_to_user(buf, &st, sizeof(st));
}

SYSCALL_DECL4(fstatat, int, dirfd, const char*, path, struct stat*, buf, int, flags)
{
    struct stat st;
    struct dentry *base;
    char *path_buf;
    long err;

    if (flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH | AT_NO_AUTOMOUNT))
        return -EINVAL;
    if (!access_ok(buf, sizeof(*buf)))
        return -EFAULT;

    path_buf = get_user_path(path);
    if (IS_ERR(path_buf))
        return PTR_ERR(path_buf);

    if (*path_buf == '\0' && (flags & AT_EMPTY_PATH)) {
        kfree(path_buf);
        if (dirfd == AT_FDCWD) {
            struct dentry *cwd = get_cwd();
            err = stat_inode(cwd->d_inode, &st);
            dput(cwd);
        } else {
            struct file *f = get_file_inc_ref(dirfd);
            if (IS_ERR(f))
                return PTR_ERR(f);
            err = vfs_stat(f, &st);
            fput(f);
        }
    } else {
        base = dirfd_base(dirfd, path_buf);
        if (IS_ERR(base)) {
            err = PTR_ERR(base);
        } else {
            err = stat_path(base, path_buf, &st, !(flags & AT_SYMLINK_NOFOLLOW));
            if (base)
                dput(base);
        }
        kfree(path_buf);
    }

    if (err < 0)
        return err;
    return copy_to_user(buf, &st, sizeof(st));
}

void setattr_copy(struct inode *inode, const struct iattr *attr)
{
    if (attr->ia_valid & ATTR_ATIME)
        inode->i_atime = attr->ia_atime;
    if (attr->ia_valid & ATTR_MTIME)
        inode->i_mtime = attr->ia_mtime;
    if (attr->ia_valid & ATTR_CTIME)
        inode->i_ctime = attr->ia_ctime;
}

// Change a file's attributes, through its filesystem if it keeps them on disk
int notify_change(struct inode *inode, const struct iattr *attr)
{
    int err = 0;

    if (inode->i_sb && IS_RDONLY(inode))
        return -EROFS;

    inode_lock(inode);
    if (inode->i_op && inode->i_op->setattr)
        err = inode->i_op->setattr(inode, attr);
    else
        setattr_copy(inode, attr);
    inode_unlock(inode);
    return err;
}

/* utimensat tv_nsec values: set that time to now, or leave it alone */
#define UTIME_NOW   ((1L << 30) - 1)
#define UTIME_OMIT  ((1L << 30) - 2)

// Set the access and modification times from times[0] and times[1], or
// both to now if times is NULL
static long set_times(struct inode *inode, const struct timespec *times)
{
    u64 now = get_unix_time();
    struct iattr attr = { .ia_valid = ATTR_CTIME, .ia_ctime = now };

    for (int i = 0; i < 2; i++) {
        long nsec = times ? times[i].tv_nsec : UTIME_NOW;
        if (nsec == UTIME_OMIT)
            continue;
        u64 t = nsec == UTIME_NOW ? now : (u64)times[i].tv_sec;
        if (i == 0) {
            attr.ia_valid |= ATTR_ATIME;
            attr.ia_atime = t;
        } else {
            attr.ia_valid |= ATTR_MTIME;
            attr.ia_mtime = t;
        }
    }
    // nothing to change, so the status time stays too
    if (attr.ia_valid == ATTR_CTIME)
        return 0;
    return notify_change(inode, &attr);
}

static long set_times_fd(int fd, const struct timespec *times)
{
    struct file *f = get_file_inc_ref(fd);
    if (IS_ERR(f))
        return PTR_ERR(f);
    struct inode *inode = f->f_inode;
    if (!inode) inode = f->f_dentry ? f->f_dentry->d_inode : NULL;
    long err = inode ? set_times(inode, times) : -EBADF;
    fput(f);
    return err;
}

static long do_utimes(int dirfd, const char *path, const struct timespec *times,
    int flags)
{
    struct dentry *base, *dentry;
    char *path_buf;
    long err;

    if (flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH))
        return -EINVAL;

    // if null path, use the file descriptor itself
    if (!path) {
        if (flags & AT_SYMLINK_NOFOLLOW)
            return -EINVAL;
        return set_times_fd(dirfd, times);
    }

    path_buf = get_user_path(path);
    if (IS_ERR(path_buf))
        return PTR_ERR(path_buf);

    // if the path is empty and AT_EMPTY_PATH is specified, use the fd
    if (*path_buf == '\0' && (flags & AT_EMPTY_PATH)) {
        kfree(path_buf);
        if (dirfd != AT_FDCWD)
            return set_times_fd(dirfd, times);
        struct dentry *cwd = get_cwd();
        err = set_times(cwd->d_inode, times);
        dput(cwd);
        return err;
    }

    base = dirfd_base(dirfd, path_buf);
    if (IS_ERR(base)) {
        kfree(path_buf);
        return PTR_ERR(base);
    }

    dentry = vfs_lookup_at(base, path_buf, !(flags & AT_SYMLINK_NOFOLLOW));
    if (base)
        dput(base);
    kfree(path_buf);
    if (IS_ERR(dentry))
        return PTR_ERR(dentry);

    err = dentry->d_inode ? set_times(dentry->d_inode, times) : -ENOENT;
    dput(dentry);
    return err;
}

SYSCALL_DECL4(utimensat, int, dirfd, const char*, path,
              const struct timespec*, times, int, flags)
{
    struct timespec ts[2];

    if (times) {
        if (copy_from_user(ts, times, sizeof(ts)))
            return -EFAULT;
        for (int i = 0; i < 2; i++) {
            long nsec = ts[i].tv_nsec;
            if (nsec != UTIME_NOW && nsec != UTIME_OMIT &&
                    (nsec < 0 || nsec >= NS_PER_SEC))
                return -EINVAL;
        }
    }
    return do_utimes(dirfd, path, times ? ts : NULL, flags);
}

SYSCALL_DECL2(utimes, const char*, path, const struct timeval*, times)
{
    struct timeval tv[2];
    struct timespec ts[2];

    if (!path)
        return -EFAULT;
    if (times) {
        if (copy_from_user(tv, times, sizeof(tv)))
            return -EFAULT;
        for (int i = 0; i < 2; i++) {
            if (tv[i].tv_usec < 0 || tv[i].tv_usec >= 1000000)
                return -EINVAL;
            ts[i] = (struct timespec){ tv[i].tv_sec, tv[i].tv_usec * NS_PER_US };
        }
    }
    return do_utimes(AT_FDCWD, path, times ? ts : NULL, 0);
}
