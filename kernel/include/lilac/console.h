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
    struct input_buffer input;
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
void console_intr(struct kbd_event);
ssize_t console_read(struct file*, void *, size_t);
ssize_t console_write(struct file*, const void *, size_t);

void console_writestring(struct console *con, const char *data);
void init_con(int num);

#endif
