#include <lilac/tty.h>

#include <stdatomic.h>
#include <lilac/lilac.h>
#include <lilac/device.h>
#include <lilac/fs.h>
#include <lilac/console.h>

ssize_t tty_read(struct file *f, void *buf, size_t count);
ssize_t tty_write(struct file *f, const void *buf, size_t count);
int tty_open(struct inode *inode, struct file *file);
int tty_release(struct inode *inode, struct file *file);

const struct file_operations tty_fops = {
    .read = tty_read,
    .write = tty_write,
    .release = tty_release,
    .flush = NULL,
    .lseek = NULL,
    .readdir = NULL,
};

const struct inode_operations tty_iops = {
    .open = tty_open,
};

//
// TTY handling
//

const struct ktermios default_termios = {
    .c_iflag = 0,
    .c_oflag = 0,
    .c_cflag = B38400 | CS8 | CREAD | HUPCL | CLOCAL,
    .c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHONL | IEXTEN,
    .c_line = 0,
    .c_cc = { [VINTR] = 3, [VERASE] = 0x7f, [VKILL] = 21, [VEOF] = 4,
              [VSTART] = 17, [VSTOP] = 19, [VSUSP] = 26, [VQUIT] = 28 },
    .c_ispeed = B38400,
    .c_ospeed = B38400
};

const struct winsize default_winsize = {
    .ws_row = 24,
    .ws_col = 80,
    .ws_xpixel = 0,
    .ws_ypixel = 0
};

static struct tty ttys[1] = {
    [0] = {
        .ops = &fbcon_tty_ops,
        .ldisc_ops = &default_tty_ldisc_ops,
        .termios = default_termios,
        .winsize = default_winsize,
        .read_wait = {
            .lock = SPINLOCK_INIT,
            .task_list = LIST_HEAD_INIT(ttys[0].read_wait.task_list),
        }
    }
};
static int active = 0;

static inline struct tty * file_get_tty(struct file *f)
{
    return ((struct tty_file_private*)f->f_data)->tty;
}

struct tty * alloc_tty_struct(void)
{
    struct tty *tty = kzmalloc(sizeof(struct tty));
    if (!tty)
        return ERR_PTR(-ENOMEM);
    atomic_store(&tty->refcount, 1);
    mutex_init(&tty->ldisc_lock);
    mutex_init(&tty->write_lock);
    spin_lock_init(&tty->files_lock);
    spin_lock_init(&tty->termios_lock);
    mutex_init(&tty->winsize_lock);
    spin_lock_init(&tty->ctrl.lock);
    tty->termios = default_termios;
    tty->winsize = default_winsize;
    return tty;
}


void tty_get(struct tty *tty)
{
    atomic_fetch_add(&tty->refcount, 1);
}

void tty_put(struct tty *tty)
{
    if (atomic_fetch_sub(&tty->refcount, 1) == 1) {
        kfree(tty);
    }
}

int tty_recv_char(char c)
{
    klog(LOG_DEBUG, "tty_recv_char: received char '%c' (0x%02x)\n", c, c);
    struct tty *tty = &ttys[active];
    if (tty->ldisc_ops && tty->ldisc_ops->receive_buf)
        tty->ldisc_ops->receive_buf(tty, (u8*)&c, NULL, 1);
    return 0;
}



// VFS interface

int tty_open(struct inode *inode, struct file *file)
{
    struct tty *tty = &ttys[0]; // TODO: select tty based on inode
    struct tty_file_private *tfp;

    spin_lock_init(&tty->ctrl.lock);
    tty->ctrl.pgrp = get_pid();

    tfp = kzmalloc(sizeof(struct tty_file_private));
    if (!tfp)
        return -ENOMEM;

    tfp->file = file;
    tfp->tty = tty;

    file->f_data = tfp;
    file->f_op = &tty_fops;

    tty->data = kzmalloc(sizeof(struct tty_data));
    tty->ops->open(tty, file);

    return 0;
}

int tty_release(struct inode *inode, struct file *file)
{
    struct tty_file_private *tfp = file->f_data;
    struct tty *tty = tfp->tty;

    kfree(tfp);
    file->f_data = NULL;
    return 0;
}

ssize_t tty_read(struct file *f, void *buf, size_t count)
{
    struct tty *tty = file_get_tty(f);

    if (!tty || !tty->ldisc_ops->read)
        return -EIO;

    return tty->ldisc_ops->read(tty, f, buf, count, f->f_pos);;
}

ssize_t tty_write(struct file *f, const void *buf, size_t count)
{
    struct tty *tty = file_get_tty(f);

    if (!tty || !tty->ldisc_ops->write)
        return -EIO;

    return tty->ldisc_ops->write(tty, NULL, buf, count);
}


void tty_init(void)
{
    add_device("/dev/tty", &tty_fops, &tty_iops);
}
