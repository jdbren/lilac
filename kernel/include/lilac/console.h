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
    u32 cx; // cursor position before write
    u32 cy;
    u32 height;
    u32 width;
    struct list_head list;
    char *data;
    struct file *file;
};

extern int write_to_screen;

void console_init(void);
ssize_t console_write(struct file *file, const void *buf, size_t count);
void console_writestring(struct console *con, const char *data);


extern const struct tty_operations fbcon_tty_ops;

#endif
