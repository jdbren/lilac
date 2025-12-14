#include <fs/fcntl.h>
#include <lilac/lilac.h>
#include <lilac/fs.h>
#include <lilac/syscall.h>
#include <lilac/sched.h>


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
