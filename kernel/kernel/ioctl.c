#include <lilac/lilac.h>
#include <lilac/syscall.h>
#include <lilac/fs.h>

SYSCALL_DECL3(ioctl, int, fd, unsigned long, op, void *, argp)
{
    long ret = 0;
    struct file *f = fget_handle(fd);
    if (IS_ERR(f))
        return PTR_ERR(f);

    if (!f->f_op->ioctl)
        ret = -ENOTTY;
    else
        ret = f->f_op->ioctl(f, op, argp);
    fput(f);
    return ret;
}
