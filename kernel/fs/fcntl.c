#include <fs/fcntl.h>
#include <lilac/lilac.h>
#include <lilac/fs.h>
#include <lilac/syscall.h>
#include <lilac/sched.h>
#include <lilac/uaccess.h>


SYSCALL_DECL3(fcntl, int, fd, int, cmd, unsigned long, arg)
{
    struct file *f = get_file_handle(fd);
    if (IS_ERR(f))
        return PTR_ERR(f);
    int new_fd;

    switch (cmd) {
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
            if (fd < 0)
                return -EBADF;
            if ((int)arg < 0)
                return -EINVAL;
            if (arg == 0)
                return vfs_dupf(fd);

            fget(f);
            new_fd = get_fd_start_at(&current->fs.files, arg, f);
            if (new_fd < 0) {
                fput(f);
                return new_fd;
            }

            // if (cmd == F_DUPFD_CLOEXEC) {
            //     f->f_mode |= O_CLOEXEC;
            // }

            return new_fd;
        case F_GETFL:
            return f->f_mode;
        case F_SETFL:
            f->f_mode = (f->f_mode & O_ACCMODE) | (arg & ~(O_ACCMODE|O_CREAT|O_EXCL|O_NOCTTY|O_TRUNC));
            return 0;
        case F_GETFD:
            return (f->f_mode & O_CLOEXEC) ? FD_CLOEXEC : 0;
        case F_SETFD:
            if (arg & FD_CLOEXEC)
                f->f_mode |= O_CLOEXEC;
            else
                f->f_mode &= ~O_CLOEXEC;
            return 0;
        default:
            return -EINVAL;
    }
}

SYSCALL_DECL2(access, const char *, pathname, int, mode)
{
    if (mode & ~7) return -EINVAL;
    const char *path = get_user_path(pathname);
    if (IS_ERR(path))
        return PTR_ERR(path);

    struct dentry *d = vfs_lookup(path);
    kfree(path);
    if (IS_ERR(d))
        return PTR_ERR(d);

    if (!d->d_inode)
        return -ENOENT;
    if (mode == 0)
        return 0;
    // TODO: full permission check
    return 0;
}

SYSCALL_DECL3(faccessat, int, dirfd, const char *, pathname, int, mode)
{
    if (!access_ok(pathname, 1)) return -EFAULT;
    if (mode & ~7) return -EINVAL;

    if (dirfd == AT_FDCWD || pathname[0] == '/')
        return sys_access(pathname, mode);

    struct file *dir = get_file_handle(dirfd);
    if (IS_ERR(dir))
        return PTR_ERR(dir);

    const char *path = get_user_path(pathname);
    if (IS_ERR(path))
        return PTR_ERR(path);

    struct dentry *d = lookup_path_from(dir->f_dentry, path);
    kfree(path);
    if (IS_ERR(d))
        return PTR_ERR(d);

    if (!d->d_inode)
        return -ENOENT;
    if (mode == 0)
        return 0;
    // TODO: full permission check
    return 0;
}
