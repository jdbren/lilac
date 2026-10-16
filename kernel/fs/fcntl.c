#include <fs/fcntl.h>
#include <lilac/lilac.h>
#include <lilac/fs.h>
#include <lilac/syscall.h>
#include <lilac/sched.h>
#include <lilac/uaccess.h>


SYSCALL_DECL3(fcntl, int, fd, int, cmd, unsigned long, arg)
{
    struct file *f = fget_handle(fd);
    if (IS_ERR(f))
        return PTR_ERR(f);
    int new_fd;
    long ret = 0;

#ifdef DEBUG_FCNTL
    klog(LOG_DEBUG, "fcntl fd=%d, cmd=%d, arg=%lu\n", fd, cmd, arg);
#endif

    switch (cmd) {
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
            if (fd < 0)
                ret = -EBADF;
            if ((int)arg < 0)
                ret = -EINVAL;
            if (ret)
                break;

            if (arg == 0) {
                ret = vfs_dupf(fd);
                break;
            }

            // the new fd holds its own reference
            fget(f);
            new_fd = get_fd_start_at(current->files, arg, f);
            if (new_fd < 0) {
                fput(f);
                ret = new_fd;
                break;
            }

            if (cmd == F_DUPFD_CLOEXEC)
                set_cloexec(current->files, new_fd);

            ret = new_fd;
            break;
        case F_GETFL:
            ret = f->f_mode;
            break;
        case F_SETFL:
            f->f_mode = (f->f_mode & O_ACCMODE) |
                (arg & ~(O_ACCMODE|O_CREAT|O_EXCL|O_NOCTTY|O_TRUNC|O_CLOEXEC));
            break;
        case F_GETFD:
            ret = is_cloexec(current->files, fd) ? FD_CLOEXEC : 0;
            break;
        case F_SETFD:
            if (arg & FD_CLOEXEC)
                set_cloexec(current->files, fd);
            else
                clear_cloexec(current->files, fd);
            break;
        default:
            ret = -EINVAL;
    }
    fput(f);
    return ret;
}

static long do_faccessat(int dirfd, const char *pathname, int mode)
{
    if (mode & ~7) return -EINVAL;
    const char *path = get_user_path(pathname);
    if (IS_ERR(path))
        return PTR_ERR(path);

    struct dentry *base = dirfd_base(dirfd, path);
    if (IS_ERR(base)) {
        kfree(path);
        return PTR_ERR(base);
    }
    struct dentry *d = vfs_lookup_at(base, path, 1);
    if (base)
        dput(base);
    kfree(path);
    if (IS_ERR(d))
        return PTR_ERR(d);

    bool exists = d->d_inode != NULL;
    dput(d);
    if (!exists)
        return -ENOENT;
    // TODO: full permission check
    return 0;
}

SYSCALL_DECL2(access, const char *, pathname, int, mode)
{
    return do_faccessat(AT_FDCWD, pathname, mode);
}

SYSCALL_DECL3(faccessat, int, dirfd, const char *, pathname, int, mode)
{
    return do_faccessat(dirfd, pathname, mode);
}
