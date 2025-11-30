#include <lilac/tty.h>

#include <lilac/lilac.h>
#include <lilac/sched.h>
#include <lilac/device.h>
#include <lilac/fs.h>
#include <lilac/console.h>
#include <lilac/syscall.h>
#include <lilac/uaccess.h>
#include <lilac/ioctl.h>
#include <drivers/keyboard.h>

#include "vt100.h"

ssize_t tty_read(struct file *f, void *buf, size_t count);
ssize_t tty_write(struct file *f, const void *buf, size_t count);
int tty_open(struct inode *inode, struct file *file);
int tty_release(struct inode *inode, struct file *file);
int tty_ioctl(struct file *f, int op, void *argp);

const struct file_operations tty_fops = {
    .read = tty_read,
    .write = tty_write,
    .release = tty_release,
    .flush = NULL,
    .lseek = NULL,
    .readdir = NULL,
    .ioctl = tty_ioctl
};

const struct inode_operations tty_iops = {
    .open = tty_open,
};

//
// TTY handling
//

const struct termios default_termios = {
    .c_iflag = 0,
    .c_oflag = 0,
    .c_cflag = B38400 | CS8 | CREAD | HUPCL | CLOCAL,
    .c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHONL | ECHOCTL | IEXTEN,
    .c_line = 0,
    .c_cc = { [VINTR] = 3, [VERASE] = 8, [VKILL] = 21, [VEOF] = 4,
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

static struct tty ttys[NUM_STATIC_TTYS] = {
    [0 ... NUM_STATIC_TTYS-1] = {
        .ops = &vt_tty_ops,
        .ldisc_ops = &default_tty_ldisc_ops,
        .termios = default_termios,
        .winsize = default_winsize,
        .termios_lock = SPINLOCK_INIT,
        .ctrl.lock = SPINLOCK_INIT,
        .read_wait.lock = SPINLOCK_INIT,
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
    // console_redraw(ttys[active].console);
}

// init remaining fields at runtime
int init_tty_struct(struct tty *tty, int i)
{
    mutex_init(&tty->ldisc_lock);
    mutex_init(&tty->write_lock);
    mutex_init(&tty->winsize_lock);
    INIT_LIST_HEAD(&tty->read_wait.task_list);
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
#ifdef DEBUG_TTY
    klog(LOG_DEBUG, "tty_recv_char: received char '%c' (0x%02x)\n", (char)c, c);
#endif
    // hack until vt switching
    // if (c & 0x80) {
    //     c &= ~0x80u;
    //     set_active_term(atoi((char*)&c));
    //     return 0;
    // }
    struct tty *tty = &ttys[active];
    if (tty->ldisc_ops && tty->ldisc_ops->receive_buf)
        tty->ldisc_ops->receive_buf(tty, &c, NULL, 1);
    return 0;
}

int tty_recv_buf(char *buf, size_t size)
{
#ifdef DEBUG_TTY
    klog(LOG_DEBUG, "tty_recv_buf: received \"%s\"\n", buf);
#endif
    struct tty *tty = &ttys[active];
    if (tty->ldisc_ops && tty->ldisc_ops->receive_buf)
        tty->ldisc_ops->receive_buf(tty, (u8*)buf, NULL, size);
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
        if (!tty) return -ENXIO;
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

    return tty->ldisc_ops->read(tty, f, buf, count, f->f_pos);
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
    umode_t mode = S_IFCHR|S_IREAD|S_IWRITE;
    dev_create("/dev/tty", &tty_fops, &tty_iops, mode, TTY_DEVICE);
    for (int i = 0; i < NUM_STATIC_TTYS; i++) {
        tty = &ttys[i];
        init_tty_struct(tty, i);
        strcpy(path, "/dev/");
        strcat(path, tty->name);
        dev_create(path, &tty_fops, &tty_iops, mode, TTY_DEVICE);
    }

    tty = &ttys[0];
    tty->vc->cury = 10;
}


static int tcgetattr(struct tty *t, struct termios *term)
{
    if (copy_to_user(term, &t->termios, sizeof(struct termios)))
        return -EFAULT;
    return 0;
}

static bool valid_baud(speed_t speed) {
    switch (speed) {
        case B0: case B50: case B75: case B110: case B134: case B150:
        case B200: case B300: case B600: case B1200: case B1800: case B2400:
        case B4800: case B9600: case B19200: case B38400: case B57600:
        case B115200: case B230400:
            return true;
        default:
            return false;
    }
}

static bool valid_cflag(tcflag_t cflag) {
    // Only allow CS5, CS6, CS7, CS8 for character size
    switch (cflag & CSIZE) {
        case CS5: case CS6: case CS7: case CS8:
            break;
        default:
            return false;
    }
    if (!valid_baud(cflag & CBAUD))
        return false;
    return true;
}

static int tcsetattr(struct tty *t, struct termios *term)
{
    struct termios tmp;
    if (copy_from_user(&tmp, term, sizeof(struct termios)))
        return -EFAULT;

    if (!valid_baud(tmp.c_ispeed) || !valid_baud(tmp.c_ospeed))
        return -EINVAL;

    if (!valid_cflag(tmp.c_cflag))
        return -EINVAL;

    struct termios old = t->termios;
    t->termios = tmp;

    if (t->ldisc_ops->set_termios)
        t->ldisc_ops->set_termios(t, &old);

    return 0;
}

static pid_t tcgetpgrp(struct tty *t)
{
    return t->ctrl.pgrp;
}

static int tcsetpgrp(struct tty *t, pid_t pgrp)
{
    if (pgrp < 0)
        return -EINVAL;
    struct task *p = get_pgrp_leader(pgrp);
    if (p && p->sid == current->sid) {
        t->ctrl.pgrp = pgrp;
        return 0;
    } else {
        klog(LOG_DEBUG, "tcsetpgrp: no such process group %d in session %d\n", pgrp, current->sid);
        return -EPERM;
    }
    return 0;
}


int tty_ioctl(struct file *f, int op, void *argp)
{
    if (!S_ISCHR(f->f_dentry->d_inode->i_mode))
        return -ENOTTY;

    struct tty *tty = file_get_tty(f);
    if (IS_ERR_OR_NULL(tty))
        return -ENOTTY;

    klog(LOG_DEBUG, "Entered tty_ioctl(0x%p,0x%x,0x%p)\n", f, op, argp);

    switch (op) {
    case TCGETS:
    case TCGETA:
        return tcgetattr(tty, argp);
    case TCSETS:
    case TCSETA:
        return tcsetattr(tty, argp);
    // todo handle drain
    case TCSETSW:
    case TCSETAW:
        return tcsetattr(tty, argp);
    case TCSETSF:
    case TCSETAF:
    // todo handle flush
        return tcsetattr(tty, argp);
    case TIOCGPGRP:
        if (access_ok(argp, sizeof(pid_t))) {
            *(pid_t*)argp = tcgetpgrp(tty);
            return 0;
        } else {
            return -EFAULT;
        }
    case TIOCSPGRP:
        if (!access_ok(argp, sizeof(pid_t)))
            return -EFAULT;
        if (current->sid != tty->ctrl.session)
            return -ENOTTY;
        return tcsetpgrp(tty, *(pid_t*)argp);
    default:
        return -EINVAL;
    }
}
