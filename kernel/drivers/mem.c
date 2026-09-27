// /dev/null and /dev/zero
#include <lilac/lilac.h>
#include <lilac/libc.h>
#include <lilac/device.h>
#include <lilac/fs.h>

static ssize_t null_read(struct file *f, void *buf, size_t count)
{
    return 0;
}

static ssize_t null_write(struct file *f, const void *buf, size_t count)
{
    return count;
}

static ssize_t zero_read(struct file *f, void *buf, size_t count)
{
    memset(buf, 0, count);
    return count;
}

static int mem_release(struct inode *inode, struct file *f)
{
    return 0;
}

static const struct file_operations null_fops = {
    .read = null_read,
    .write = null_write,
    .release = mem_release,
};

static const struct file_operations zero_fops = {
    .read = zero_read,
    .write = null_write,
    .release = mem_release,
};

static int null_open(struct inode *inode, struct file *f)
{
    f->f_op = &null_fops;
    return 0;
}

static int zero_open(struct inode *inode, struct file *f)
{
    f->f_op = &zero_fops;
    return 0;
}

static const struct inode_operations null_iops = { .open = null_open };
static const struct inode_operations zero_iops = { .open = zero_open };

void mem_devices_init(void)
{
    dev_create("/dev/null", &null_fops, &null_iops, S_IFCHR|S_IREAD|S_IWRITE, NULL_DEVICE);
    dev_create("/dev/zero", &zero_fops, &zero_iops, S_IFCHR|S_IREAD|S_IWRITE, NULL_DEVICE);
}
