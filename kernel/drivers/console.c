#include <lilac/console.h>
#include <lilac/tty.h>
#include <lilac/lilac.h>
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

char default_buf[30*80];

struct console consoles[8] = {
    [0 ... 7] = {
        .lock = SPINLOCK_INIT,
        .cx = 0,
        .cy = 0,
        .height = 30,
        .width = 80,
        .data = default_buf,
        .first_row = 0
    }
};

#define con_pos(con) ((( ((con)->first_row + (con)->cy) % (con)->height ) * (con)->width ) + (con)->cx)

void console_newline(struct console *con)
{
    con->cx = 0;
    if ((con->cy += 1) >= con->height) {
        graphics_scroll();
        con->cy = con->height - 1;
        u32 phys_r = (con->first_row + con->cy) % con->height;
        memset(&con->data[phys_r * con->width], ' ', con->width);
    }
}

/* Put a character at an absolute position (x, y) */
void console_putchar_at(struct console *con, int c, int x, int y)
{
    if (x < 0 || x >= con->width || y < 0 || y >= con->height)
        return;  // out of bounds, ignore

    int pos = y * con->width + x;
    con->data[pos] = (char)c;

    graphics_putc((u16)c, x, y);
}


void console_putchar(struct console *con, int c)
{
    if (c == '\n') {
        graphics_putc(' ', con->cx, con->cy);
        console_newline(con);
        return;
    } else if (c == '\t') {
        do {
            console_putchar(con, ' ');
        } while (con->cx % 8 && con->cx < con->width);
        return;
    } else if (c == '\b') {
        if (con->cx > 0) {
            graphics_putc(' ', con->cx, con->cy);
            con->cx--;
            con->data[con_pos(con)] = ' ';
        }
        return;
    }
    con->data[con_pos(con)] = (char)c;
    graphics_putc((u16)c, con->cx, con->cy);
    if (++(con->cx) >= con->width) {
        graphics_putc(' ', con->cx, con->cy);
        console_newline(con);
    }
}

void console_redraw(struct console *con)
{
    for (u32 y = 0; y < con->height; y++) {
        u32 phys_r = (con->first_row + y) % con->height;
        for (u32 x = 0; x < con->width; x++) {
            char c = con->data[phys_r * con->width + x];
            graphics_putc(c, x, y);
        }
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
    memset(con->data, ' ', con->height*con->width);
    con->cx = 0;
    con->cy = 0;
}

ssize_t console_write(struct file *file, const void *buf, size_t count)
{
    char *bufp = (char*)buf;
    struct console *con = &consoles[0];
    acquire_lock(&con->lock);
    for (size_t i = 0; i < count; i++)
        console_putchar(con, bufp[i] & 0xff);
    release_lock(&con->lock);
    return count;
}

void console_init(void)
{
    struct console *con = &consoles[0];
    console_clear(con);
    print_system_info();
    write_to_screen = 0;
}

int fbcon_open(struct tty *tty, struct file *file)
{
    // struct console *con = &consoles[tty->index];
    // if (!tty->console) {
    //     tty->console = con;
    //     con->data = kmalloc(con->height * con->width);
    //     if (!con->data)
    //         panic("Out of memory");
    //     memset(con->data, ' ', con->height * con->width);
    //     memcpy(con->data, "HELLO WORLD!", 12);

    // }

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
