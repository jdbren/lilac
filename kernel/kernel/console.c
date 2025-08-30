#include <lilac/console.h>
#include <lilac/tty.h>
#include <lilac/lilac.h>
#include <lilac/keyboard.h>
#include <lilac/timer.h>
#include <lilac/device.h>
#include <lilac/fs.h>
#include <drivers/framebuffer.h>
#include <lilac/kmm.h>

#include <cpuid.h>


#if (!defined(DEBUG_KMM) && !defined(DEBUG_PAGING))
int write_to_screen = 1;
#else
int write_to_screen = 0;
#endif

static struct console console = {
    .lock = SPINLOCK_INIT,
    .cx = 0,
    .cy = 0,
    .height = 30,
    .width = 80,
    .list = LIST_HEAD_INIT(console.list),
    .data = NULL,
    .file = NULL
};


void console_newline(struct console *con)
{
    con->cx = 0;
    if ((con->cy += 1) >= con->height) {
        graphics_scroll();
        con->cy = con->height - 1;
    }
}

void console_putchar(struct console *con, int c)
{
    if (c == '\n') {
        console_newline(con);
        return;
    } else if (c == '\t') {
        do {
            console_putchar(con, ' ');
        } while (con->cx % 8 && con->cx < con->width);
        return;
    } else if (c == '\b') {
        if (con->cx > 0) {
            con->cx--;
            graphics_putc(' ', con->cx, con->cy);
        }
        return;
    }
    graphics_putc((u16)c, con->cx, con->cy);
    if (++(con->cx) >= con->width) {
        console_newline(con);
    }
}

void console_writestring(struct console *con, const char *data)
{
    while (*data) {
        console_putchar(con, *data++);
    }
}

void console_clear(struct console *con)
{
    graphics_clear();
    con->cx = 0;
    con->cy = 0;
}

ssize_t console_write(struct file *file, const void *buf, size_t count)
{
    char *bufp = (char*)buf;
    struct console *con = &console;
    acquire_lock(&con->lock);
    for (size_t i = 0; i < count; i++)
        console_putchar(con, bufp[i] & 0xff);
    release_lock(&con->lock);
    return count;
}

void console_init(void)
{
    // add_device("/dev/console", &console_fops);
    struct console *con = &console;

    console_clear(con);
    graphics_setcolor(RGB_MAGENTA, RGB_BLACK);
    console_writestring(con, "LilacOS v0.1.0\n\n");

    graphics_setcolor(RGB_CYAN, RGB_BLACK);
    unsigned int regs[12];
    char str[sizeof(regs) + 1];
    memset(str, 0, sizeof(str));

    __cpuid(0, regs[0], regs[1], regs[2], regs[3]);
    memcpy(str, &regs[1], 4);
    memcpy(str + 4, &regs[3], 4);
    memcpy(str + 8, &regs[2], 4);
    str[12] = '\0';
    printf("CPU Vendor: %s, ", str);

    __cpuid(0x80000000, regs[0], regs[1], regs[2], regs[3]);

    if (regs[0] < 0x80000004)
        return;

    __cpuid(0x80000002, regs[0], regs[1], regs[2], regs[3]);
    __cpuid(0x80000003, regs[4], regs[5], regs[6], regs[7]);
    __cpuid(0x80000004, regs[8], regs[9], regs[10], regs[11]);

    memcpy(str, regs, sizeof(regs));
    str[sizeof(regs)] = '\0';
    printf("%s\n", str);

    __cpuid(0x80000005, regs[0], regs[1], regs[2], regs[3]);
    printf("Cache line: %u bytes, ", regs[2] & 0xff);
    printf("L1 Cache: %u KB, ", regs[2] >> 24);

    __cpuid(0x80000006, regs[0], regs[1], regs[2], regs[3]);
    printf("L2 Cache: %u KB\n", regs[2] >> 16);

    size_t mem_sz_mb = arch_get_mem_sz() / 0x400;
    printf("Memory: %u MB\n", mem_sz_mb);

    u64 sys_time_ms = get_sys_time() / 1000000;
    printf("System clock running for %llu ms\n", sys_time_ms);
    struct timestamp ts = get_timestamp();
    printf("Current time: " TIME_FORMAT "\n\n",
        ts.year, ts.month, ts.day, ts.hour, ts.minute, ts.second);
    graphics_setcolor(RGB_WHITE, RGB_BLACK);
    write_to_screen = 0;
}

int fbcon_open(struct tty *tty, struct file *file)
{
    tty->console = &console;
    return 0;
}

ssize_t fbcon_write(struct tty *tty, const u8 *buf, size_t count)
{
    char *bufp = (char*)buf;
    struct console *con = tty->console;
    acquire_lock(&con->lock);
    for (size_t i = 0; i < count; i++)
        console_putchar(con, bufp[i] & 0xff);
    release_lock(&con->lock);
    return count;
}

const struct tty_operations fbcon_tty_ops = {
    .open = fbcon_open,
    .close = NULL,
    .write = fbcon_write,
};
