#include <mm/kheap.h>
#include <utility/multiboot2.h>
#include "paging.h"

#define SSFN_CONSOLEBITMAP_CONTROL
#define SSFN_IMPLEMENTATION
#define SSFN_CONSOLEBITMAP_TRUECOLOR        /* use the special renderer for 32 bit truecolor packed pixels */
#define SSFN_realloc krealloc
#define SSFN_free kfree

#include <utility/ssfn.h>
#include <utility/VGA9.h>

ssfn_t ctx = {0};
ssfn_buf_t buf;

void graphics_putchar(char c)
{
    ssfn_render(&ctx, &buf, &c);
}

void graphics_writestring(const char* data)
{
    while (*data)
        ssfn_render(&ctx, &buf, data++);
}

void graphics_init(struct multiboot_tag_framebuffer *fb)
{
    if (ssfn_load(&ctx, &VGA9))
		printf("error loading font\n");    /* load the font */
	if (ssfn_select(&ctx, SSFN_FAMILY_ANY, NULL, SSFN_STYLE_REGULAR, 16))
		printf("error selecting font\n");    /* select a font */

	buf.ptr = (u8*)fb->common.framebuffer_addr;    /* address of the linear frame buffer */
	buf.w = fb->common.framebuffer_width;     /* width */
	buf.h = fb->common.framebuffer_height;    /* height */
	buf.p = fb->common.framebuffer_pitch;     /* bytes per line */
	buf.x = 0;                					/* pen position */
    buf.y = 20;
	buf.fg = 0xFF0AECFC;                     				/* foreground color */

	map_pages(buf.ptr, buf.ptr, PG_WRITE, (buf.p * buf.h) / PAGE_BYTES);
	memset(buf.ptr, 0, buf.p * buf.h);

	graphics_writestring("Graphics mode terminal initialized\n");
}