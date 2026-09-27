// /dev/kmsg: lets userspace write into the kernel log (debugcon/serial).
#include <lilac/lilac.h>
#include <lilac/libc.h>
#include <lilac/log.h>
#include <lilac/device.h>
#include <lilac/fs.h>

static ssize_t kmsg_write(struct file *f, const void *buf, size_t count)
{
    const char *s = buf;

    klog_lock();
    for (size_t i = 0; i < count; i++)
        putchar(s[i]);
    klog_unlock();

    return count;
}

static ssize_t kmsg_read(struct file *f, void *buf, size_t count)
{
    return 0;
}

static int kmsg_release(struct inode *inode, struct file *f)
{
    return 0;
}

static const struct file_operations kmsg_fops = {
    .read = kmsg_read,
    .write = kmsg_write,
    .release = kmsg_release,
};

static int kmsg_open(struct inode *inode, struct file *f)
{
    f->f_op = &kmsg_fops;
    return 0;
}

static const struct inode_operations kmsg_iops = {
    .open = kmsg_open,
};

void kmsg_init(void)
{
    dev_create("/dev/kmsg", &kmsg_fops, &kmsg_iops, S_IFCHR|S_IREAD|S_IWRITE, NULL_DEVICE);
}
