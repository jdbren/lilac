#ifndef KERNEL_TTY_H
#define KERNEL_TTY_H

#include <stddef.h>
#include <utility/multiboot2.h>
#include <lilac/types.h>

#define RGB_WHITE 0xFFFFFFFF
#define RGB_BLACK 0x00000000
#define RGB_RED 0xFF0000
#define RGB_GREEN 0x00FF00
#define RGB_BLUE 0x0000FF
#define RGB_CYAN 0x00FFFF
#define RGB_MAGENTA 0xFF00FF
#define RGB_YELLOW 0xFFFF00
#define RGB_GRAY 0x808080
#define RGB_DARK_GRAY 0x404040
#define RGB_LIGHT_GRAY 0xC0C0C0

void terminal_init(void);
void terminal_putchar(char c);
void terminal_write(const char *data, size_t size);
void terminal_writestring(const char *data);

void graphics_init(struct multiboot_tag_framebuffer *fb);
void graphics_putchar(char c);
void graphics_writestring(const char *data);
void graphics_setcolor(u32 fg, u32 bg);
void graphics_clear(void);

#endif
