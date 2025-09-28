#include <lilac/lilac.h>
#include <lilac/fs.h>
#include <lilac/file.h>
#include <lilac/syscall.h>
#include <lilac/sched.h>


SYSCALL_DECL3(fcntl, int, fd, int, cmd, unsigned long, arg)
{
    struct file *f;
    int new_fd;

    switch (cmd) {
        case F_DUPFD:
        case F_DUPFD_CLOEXEC:
            if (fd < 0)
                return -EBADF;
            if (arg < 0)
                return -EINVAL;
            if (arg == 0)
                return vfs_dupf(fd);

            new_fd = get_fd_at(&current->fs.files, arg);
            if (new_fd < 0)
                return new_fd;
            new_fd = vfs_dup(fd, new_fd);
            if (new_fd < 0)
                return new_fd;

            if (cmd == F_DUPFD_CLOEXEC) {
                f = get_file_handle(new_fd);
                f->f_mode |= O_CLOEXEC;
            }

            return new_fd;
        case F_GETFL:
            f = get_file_handle(fd);
            if (IS_ERR(f))
                return PTR_ERR(f);
            return f->f_mode;
        case F_SETFL:
            f = get_file_handle(fd);
            if (IS_ERR(f))
                return PTR_ERR(f);
            f->f_mode = (f->f_mode & O_ACCMODE) | (arg & ~(O_ACCMODE|O_CREAT|O_EXCL|O_NOCTTY|O_TRUNC));
            return 0;
        case F_GETFD:
            f = get_file_handle(fd);
            if (IS_ERR(f))
                return PTR_ERR(f);
            return (f->f_mode & O_CLOEXEC) ? FD_CLOEXEC : 0;
        case F_SETFD:
            f = get_file_handle(fd);
            if (IS_ERR(f))
                return PTR_ERR(f);
            if (arg & FD_CLOEXEC)
                f->f_mode |= O_CLOEXEC;
            else
                f->f_mode &= ~O_CLOEXEC;
            return 0;
        default:
            return -EINVAL;
    }
}
