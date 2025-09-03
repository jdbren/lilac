#ifndef _CONSOLE_H
#define _CONSOLE_H

#include <lilac/types.h>
#include <lilac/sync.h>
#include <lilac/keyboard.h>

struct file;

#define INPUT_BUF_SIZE 256

struct input_buffer {
    char buf[INPUT_BUF_SIZE];
    int rpos;
    int wpos;
    int epos;
};

struct console {
    spinlock_t lock;
    u32 cx, cy; //cursor
    u32 height, width;
    char *data;
    int first_row;
};

extern int write_to_screen;

void console_init(void);
ssize_t console_write(struct file *file, const void *buf, size_t count);
void console_writestring(struct console *con, const char *data);
void console_redraw(struct console *con);
void console_newline(struct console *con);
void console_putchar_at(struct console *con, int c, int x, int y);
void console_putchar(struct console *con, int c);
void console_clear(struct console *con);

extern struct console consoles[8];
extern const struct tty_operations fbcon_tty_ops;

#endif
