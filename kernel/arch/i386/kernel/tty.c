// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <stdbool.h>
#include <string.h>
#include <kernel/tty.h>
#include <kernel/types.h>
#include <kernel/panic.h>
#include <mm/kheap.h>
#include <utility/vga.h>
#include "paging.h"

#define SSFN_IMPLEMENTATION
#define SSFN_CONSOLEBITMAP_TRUECOLOR        /* use the special renderer for 32 bit truecolor packed pixels */
#define SSFN_realloc krealloc
#define SSFN_free kfree

#include <utility/ssfn.h>
#include <utility/VGA9.h>

ssfn_t ctx = {0};
ssfn_buf_t buf;

static void graphics_scroll(void);

void graphics_putchar(char c)
{
	if (c == '\n') {
		buf.x = 0;
		if ((buf.y += ctx.f->height) >= buf.h)
			graphics_scroll();
		return;
	}
    ssfn_render(&ctx, &buf, &c);
	if (buf.x >= buf.w) {
		buf.x = 0;
		if ((buf.y += ctx.f->height) >= buf.h)
			graphics_scroll();
	}
}

void graphics_writestring(const char* data)
{
    while (*data)
        ssfn_render(&ctx, &buf, data++);
}

void graphics_init(struct multiboot_tag_framebuffer *fb)
{
	if (fb->common.framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB
		|| fb->common.framebuffer_bpp != 32) {
		kerror("Unsupported framebuffer type\n");
	}

    if (ssfn_load(&ctx, &VGA9))
		kerror("error loading font\n");    /* load the font */
	if (ssfn_select(&ctx, SSFN_FAMILY_ANY, NULL, SSFN_STYLE_REGULAR, 16))
		kerror("error selecting font\n");    /* select a font */

	buf.ptr = (u8*)fb->common.framebuffer_addr;    /* address of the linear frame buffer */
	buf.w = fb->common.framebuffer_width;     /* width */
	buf.h = fb->common.framebuffer_height;    /* height */
	buf.p = fb->common.framebuffer_pitch;     /* bytes per line */
	buf.x = 0;                					/* pen position */
    buf.y = 16;
	buf.fg = 0xFF0AECFC;                     				/* foreground color */

	map_pages(buf.ptr, buf.ptr, PG_WRITE, (buf.p * buf.h) / PAGE_BYTES);
	memset(buf.ptr, 0, buf.p * buf.h);

	graphics_writestring("Graphics mode terminal initialized\n");
}

static void graphics_scroll(void)
{
	u8 *dst, *src;
	u32 line_size = buf.p * ctx.f->height;

	for (dst = buf.ptr, src = buf.ptr + line_size;
		src < buf.ptr + buf.p * buf.h;
		dst += line_size, src += line_size
	){
		memcpy(dst, src, line_size);
	}
	memset(dst, 0, line_size);
	buf.y -= ctx.f->height;
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
