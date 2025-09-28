#include <lilac/lilac.h>
#include <lilac/syscall.h>
#include <lilac/fs.h>

SYSCALL_DECL3(ioctl, int, fd, unsigned long, op, void *, argp)
{
    struct file *f = get_file_handle(fd);
    if (IS_ERR(f))
        return PTR_ERR(f);

    if (!f->f_op->ioctl)
        return -ENOTTY;

    return f->f_op->ioctl(f, op, argp);
}
