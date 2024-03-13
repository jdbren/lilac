#ifndef KERNEL_TTY_H
#define KERNEL_TTY_H

#include <stddef.h>
#include <utility/multiboot2.h>

void terminal_init(void);
void terminal_putchar(char c);
void terminal_write(const char *data, size_t size);
void terminal_writestring(const char *data);

void graphics_init(struct multiboot_tag_framebuffer *fb);
void graphics_putchar(char c);
void graphics_writestring(const char *data);

#endif
