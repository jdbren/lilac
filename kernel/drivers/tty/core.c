#include <lilac/tty.h>

#include <stdatomic.h>
#include <lilac/lilac.h>
#include <lilac/sched.h>
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

#define NUM_STATIC_TTYS 8

static struct tty ttys[NUM_STATIC_TTYS] = {
    [0 ... NUM_STATIC_TTYS-1] = {
        .ops = &fbcon_tty_ops,
        .ldisc_ops = &default_tty_ldisc_ops,
        .termios = default_termios,
        .winsize = default_winsize,
    }
};
static spinlock_t ttys_lock = SPINLOCK_INIT;
static int active = 0;

static inline struct tty * file_get_tty(struct file *f)
{
    return ((struct tty_file_private*)f->f_data)->tty;
}

void set_active_term(int x)
{
    active = x;
    klog(LOG_DEBUG, "Set term to %d\n", x);
    console_redraw(ttys[active].console);
}

int init_tty_struct(struct tty *tty, int i)
{
    mutex_init(&tty->ldisc_lock);
    mutex_init(&tty->write_lock);
    spin_lock_init(&tty->termios_lock);
    mutex_init(&tty->winsize_lock);
    spin_lock_init(&tty->ctrl.lock);
    spin_lock_init(&tty->read_wait.lock);
    INIT_LIST_HEAD(&tty->read_wait.task_list);
    tty->termios = default_termios;
    tty->winsize = default_winsize;
    tty->ctrl.pgrp = 0;
    tty->ctrl.session = 0;
    tty->data = kzmalloc(sizeof(*tty->data));
    tty->index = i;
    strcpy(tty->name, "tty");
    tty->name[3] = i + '0';
    tty->name[4] = '\0';
    tty->ops->open(tty, NULL);
    return 0;
}


void tty_get(struct tty *tty)
{
    atomic_fetch_add(&tty->refcount, 1);
}

void tty_put(struct tty *tty)
{
    atomic_fetch_sub(&tty->refcount, 1);
}

int tty_recv_char(u8 c)
{
    klog(LOG_DEBUG, "tty_recv_char: received char '%c' (0x%02x)\n", (char)c, c);
    // hack until vt switching
    if (c & 0x80) {
        c &= ~0x80u;
        set_active_term(atoi((char*)&c));
        return 0;
    }
    struct tty *tty = &ttys[active];
    if (tty->ldisc_ops && tty->ldisc_ops->receive_buf)
        tty->ldisc_ops->receive_buf(tty, &c, NULL, 1);
    return 0;
}

int tty_recv_buf(char *buf, size_t size)
{
    struct tty *tty = &ttys[active];
    if (tty->ldisc_ops && tty->ldisc_ops->receive_buf)
        tty->ldisc_ops->receive_buf(tty, buf, NULL, size);
    return 0;
}



// VFS interface

int tty_open(struct inode *inode, struct file *file)
{
    struct tty *tty;
    struct tty_file_private *tfp;

    const char *name = file->f_dentry->d_name;
    char lastc = name[strlen(name)-1];
    int index = -1;

    if (!strcmp("tty", name)) {
        tty = current->ctty;
    } else {
        index = atoi(&lastc);
        if (index >= NUM_STATIC_TTYS || index < 0)
            return -ENXIO;
        tty = &ttys[index];
    }

    acquire_lock(&tty->ctrl.lock);
    tty_get(tty);
    if (tty->refcount == 1) {
        tty->ctrl.session = current->sid;
        tty->ctrl.pgrp = current->pgid;
        current->ctty = tty;
    }
    release_lock(&tty->ctrl.lock);

    tfp = kzmalloc(sizeof(struct tty_file_private));
    if (!tfp)
        return -ENOMEM;

    tfp->file = file;
    tfp->tty = tty;

    file->f_data = tfp;
    file->f_op = &tty_fops;

    return 0;
}

int tty_release(struct inode *inode, struct file *file)
{
    struct tty_file_private *tfp = file->f_data;
    struct tty *tty = tfp->tty;
    tty_put(tty);
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
    struct tty *tty;
    char path[32];
    add_device("/dev/tty", &tty_fops, &tty_iops);
    for (int i = 0; i < NUM_STATIC_TTYS; i++) {
        tty = &ttys[i];
        init_tty_struct(tty, i);
        strcpy(path, "/dev/");
        strcat(path, tty->name);
        klog(LOG_DEBUG, "Creating %s\n", path);
        add_device(path, &tty_fops, &tty_iops);
    }
}
