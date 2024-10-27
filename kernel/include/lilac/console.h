#ifndef _CONSOLE_H
#define _CONSOLE_H

#include <lilac/types.h>

struct file;

void console_init(void);
void console_intr(char);
ssize_t console_read(struct file*, void *, size_t);
ssize_t console_write(struct file*, const void *, size_t);

#endif
