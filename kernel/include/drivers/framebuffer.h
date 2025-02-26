#ifndef _FRAMEBUFFER_H
#define _FRAMEBUFFER_H

#include <lilac/types.h>
#include <utility/multiboot2.h>

#define RGB_WHITE 0xFFFFFFFF
#define RGB_BLACK 0xFF000000
#define RGB_RED 0xFF0000
#define RGB_GREEN 0x00FF00
#define RGB_BLUE 0x0000FF
#define RGB_CYAN 0x00FFFF
#define RGB_MAGENTA 0xFF00FF
#define RGB_YELLOW 0xFFFF00
#define RGB_GRAY 0x808080
#define RGB_DARK_GRAY 0x404040
#define RGB_LIGHT_GRAY 0xC0C0C0

struct framebuffer_color {
    u32 fg;
    u32 bg;
};

struct framebuffer {
    u8 *fb;
    s32 fb_width;
    s32 fb_height;
    u16 fb_pitch;
    u32 fb_fg;
    u32 fb_bg;
};

void graphics_init(struct multiboot_tag_framebuffer *fb);
void graphics_putc(u16 c, u32 cx, u32 cy);
void graphics_clear(void);
void graphics_scroll(void);

struct framebuffer_color graphics_getcolor(void);
void graphics_setcolor(u32 fg, u32 bg);

#endif
