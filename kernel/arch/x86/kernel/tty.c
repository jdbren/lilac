// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <stdbool.h>
#include <string.h>
#include <lilac/tty.h>
#include <lilac/types.h>
#include <lilac/log.h>
#include <lilac/panic.h>
#include <mm/kmalloc.h>
#include <mm/kmm.h>
#include <utility/vga.h>

#define WINDOW_BORDER 16

// #define SSFN_IMPLEMENTATION
#define SSFN_CONSOLEBITMAP_TRUECOLOR /* renderer for 32 bit truecolor */
// #define SSFN_CONSOLEBITMAP_CONTROL
#define SSFN_realloc krealloc
#define SSFN_free kfree

#include <utility/ssfn.h>
#include <utility/VGA9.h>

// ssfn_t ctx = {0};
// ssfn_buf_t buf;

static void graphics_scroll(void);
//static void graphics_delchar(void);

void graphics_putchar(char c)
{
    if (c == '\n') {
        ssfn_dst.x = WINDOW_BORDER;
        if ((ssfn_dst.y += ssfn_src->height) >= ssfn_dst.h - WINDOW_BORDER)
            graphics_scroll();
        return;
    }
    else if (c == '\t') {
        ssfn_dst.x += 4 * ssfn_src->width;
        if (ssfn_dst.x >= ssfn_dst.w - WINDOW_BORDER) {
            ssfn_dst.x = WINDOW_BORDER;
            if ((ssfn_dst.y += ssfn_src->height) >= ssfn_dst.h - WINDOW_BORDER)
                graphics_scroll();
        }
        return;
    }
    else if (c == '\r') {
        ssfn_dst.x = WINDOW_BORDER;
        return;
    }
    else if (c == '\b') {
        // graphics_delchar();
        return;
    }
    ssfn_putc(c);
    if (ssfn_dst.x >= ssfn_dst.w - WINDOW_BORDER) {
        ssfn_dst.x = WINDOW_BORDER;
        if ((ssfn_dst.y += ssfn_src->height) >= ssfn_dst.h - WINDOW_BORDER)
            graphics_scroll();
    }
}

void graphics_writestring(const char* data)
{
    while (*data)
        graphics_putchar(*data++);
}

void graphics_init(struct multiboot_tag_framebuffer *fb)
{
    if (fb->common.framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB
        || fb->common.framebuffer_bpp != 32) {
        kerror("Unsupported framebuffer type\n");
    }

    ssfn_src = (ssfn_font_t*)&VGA9;

    // if (ssfn_load(&ctx, &VGA9))
    // 	kerror("error loading font\n");    /* load the font */
    // if (ssfn_select(&ctx, SSFN_FAMILY_ANY, NULL, SSFN_STYLE_REGULAR, 16))
    // 	kerror("error selecting font\n");    /* select a font */

    ssfn_dst.w = fb->common.framebuffer_width;     /* width */
    ssfn_dst.h = fb->common.framebuffer_height;    /* height */
    ssfn_dst.p = fb->common.framebuffer_pitch;     /* bytes per line */
    ssfn_dst.x = WINDOW_BORDER;                	   /* pen position */
    ssfn_dst.y = WINDOW_BORDER;
    ssfn_dst.fg = RGB_LIGHT_GRAY;                  /* foreground color */
    ssfn_dst.bg = RGB_BLACK;                       /* background color */

    void *vframebuf = map_phys((void*)fb->common.framebuffer_addr,
                        ssfn_dst.p * ssfn_dst.h, PG_WRITE);
    ssfn_dst.ptr = vframebuf; 			/* pointer to the framebuffer */
    memset(ssfn_dst.ptr, 0, ssfn_dst.p * ssfn_dst.h);

    kstatus(STATUS_OK, "Graphics mode terminal initialized\n");
}

static void graphics_scroll(void)
{
    u8 *dst, *src;
    u32 line_size = ssfn_dst.p * ssfn_src->height;

    for (dst = ssfn_dst.ptr, src = ssfn_dst.ptr + line_size;
            src < ssfn_dst.ptr + ssfn_dst.p * ssfn_dst.h;
            dst += line_size, src += line_size) {
        memcpy(dst, src, line_size);
    }
    memset(dst, 0, line_size);
    ssfn_dst.y -= ssfn_src->height;
}

/*
void graphics_delchar(void)
{
    if (ssfn_dst.x == WINDOW_BORDER) {
        if (ssfn_dst.y == WINDOW_BORDER)
            return;
        ssfn_dst.x = ssfn_dst.w - WINDOW_BORDER - ssfn_src->width;
        ssfn_dst.y -= ssfn_src->height;
    }
    else
        ssfn_dst.x -= ssfn_src->width;
    // Clear the character
    u32 color = ssfn_dst.fg;
    ssfn_dst.fg = ssfn_dst.bg;
    ssfn_putc(' ');
    ssfn_dst.fg = color;
    if (ssfn_dst.x == WINDOW_BORDER) {
        if (ssfn_dst.y == WINDOW_BORDER)
            return;
        ssfn_dst.x = ssfn_dst.w - WINDOW_BORDER - ssfn_src->width;
        ssfn_dst.y -= ssfn_src->height;
    }
    else
        ssfn_dst.x -= ssfn_src->width;
}
*/

void graphics_clear(void)
{
    memset(ssfn_dst.ptr, 0, ssfn_dst.p * ssfn_dst.h);
    ssfn_dst.x = WINDOW_BORDER;
    ssfn_dst.y = WINDOW_BORDER;
}

struct console_color graphics_getcolor(void)
{
    struct console_color color = {ssfn_dst.fg, ssfn_dst.bg};
    return color;
}

void graphics_setcolor(u32 fg, u32 bg)
{
    ssfn_dst.fg = fg;
    ssfn_dst.bg = bg;
}



static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;
static u16* const VGA_MEMORY = (u16*)0xB8000;

static size_t terminal_row;
static size_t terminal_column;
static u8 terminal_color;
static u16* terminal_buffer;

void terminal_init(void)
{
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = vga_entry_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    terminal_buffer = VGA_MEMORY;
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }
    terminal_writestring("Terminal initialized\n");
}

void terminal_setcolor(u8 color)
{
    terminal_color = color;
}

void terminal_putentryat(unsigned char c, u8 color, size_t x, size_t y)
{
    const size_t index = y * VGA_WIDTH + x;
    terminal_buffer[index] = vga_entry(c, color);
}

void terminal_scroll(void)
{
    size_t i, k, j;
    for (k = 1, j = 0; k < VGA_HEIGHT; k++, j++) {
        for (i = 0; i < VGA_WIDTH; i++) {
            terminal_putentryat(terminal_buffer[k * VGA_WIDTH + i],
                terminal_color, i, j);
        }
    }
    for (i = 0; i < VGA_WIDTH; i++)
        terminal_putentryat(' ', terminal_color, i, VGA_HEIGHT-1);
    terminal_row--;
}

void terminal_putchar(char c)
{
    unsigned char uc = c;
    if (c == '\n') {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT)
            terminal_scroll();
        return;
    }
    terminal_putentryat(uc, terminal_color, terminal_column, terminal_row);
    if (++terminal_column == VGA_WIDTH) {
        terminal_column = 0;
        if (++terminal_row == VGA_HEIGHT)
            terminal_scroll();
    }
}

void terminal_write(const char* data, size_t size)
{
    for (size_t i = 0; i < size; i++)
        terminal_putchar(data[i]);
}

void terminal_writestring(const char* data)
{
    terminal_write(data, strlen(data));
}

void terminal_clear()
{
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }
    terminal_row = 0;
    terminal_column = 0;
}
