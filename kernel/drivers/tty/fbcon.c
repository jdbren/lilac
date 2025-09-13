#include <lilac/console.h>
#include <lilac/libc.h>
#include <lilac/tty.h>
#include <lilac/err.h>
#include <drivers/framebuffer.h>

#pragma GCC diagnostic ignored "-Wsign-compare"


void fbcon_init(struct vc_state *vc, int)
{
    extern struct framebuffer *fb;
    vc->display_data = fb;
    vc->screen_buf = fb->fb;
}


static inline u8* fb_get_char_pixel(struct framebuffer *fb, int y, int x, int pixel_y)
{
    y = y * fb->font->height + pixel_y;
    x = x * fb->font->width;
    return fb->fb + (y * fb->fb_pitch) + (x * fb->bypp);
}

static void fb_clear_rect_pixels(struct framebuffer *fb, int x, int y, int width, int height)
{
    for (int py = 0; py < height; py++) {
        u8 *row = fb->fb + ((y + py) * fb->fb_pitch) + (x * fb->bypp);
        for (int px = 0; px < width; px++) {
            *(u32 *)(row + (px * fb->bypp)) = fb->fb_bg;
        }
    }
}

/**
 * Clear a rectangular region of characters
 * @param fb framebuffer structure
 * @param row starting character row
 * @param col starting character column
 * @param height height in characters
 * @param width width in characters
 */
void fbcon_clear_region(struct vc_state *vc, int row, int col, int height, int width)
{
    struct framebuffer *fb = vc->display_data;
    if (!fb || !fb->fb || !fb->font || height <= 0 || width <= 0)
        return;

    // Convert to pixels and clamp to framebuffer bounds
    col *= fb->font->width;
    row *= fb->font->height;
    width *= fb->font->width;
    height *= fb->font->height;

    if (col >= fb->fb_width || row >= fb->fb_height)
        return;

    if (col + width > fb->fb_width)
        width = fb->fb_width - col;
    if (row + height > fb->fb_height)
        height = fb->fb_height - row;

    fb_clear_rect_pixels(fb, col, row, width, height);
}

/**
 * Move/shift characters in the framebuffer
 * @param fb framebuffer structure
 * @param sy source row
 * @param sx source column
 * @param dy destination row
 * @param dx destination column
 * @param height height in characters to move
 * @param width width in characters to move
 *
 * For scrolling, set appropriate src/dst rows with full width.
 * For horizontal shifts within a line, set same src/dst rows with different columns.
 */
void fbcon_move_chars(struct vc_state *vc,
                     int sy, int sx,
                     int dy, int dx,
                     int height, int width)
{
    struct framebuffer *fb = vc->display_data;
    if (!fb || !fb->fb || !fb->font || height <= 0 || width <= 0)
        return;

    int char_h = fb->font->height;
    int copy_bytes = width * fb->font->width * fb->bypp;

    if ((dy > sy) || (dy == sy && dx > sx)) {
        for (int row = height - 1; row >= 0; row--) {
            for (int py = char_h - 1; py >= 0; py--) {
                u8 *src = fb_get_char_pixel(fb, sy + row, sx, py);
                u8 *dst = fb_get_char_pixel(fb, dy + row, dx, py);
                memmove(dst, src, copy_bytes);
            }
        }
    } else {
        for (int row = 0; row < height; row++) {
            for (int py = 0; py < char_h; py++) {
                u8 *src = fb_get_char_pixel(fb, sy + row, sx, py);
                u8 *dst = fb_get_char_pixel(fb, dy + row, dx, py);
                memmove(dst, src, copy_bytes);
            }
        }
    }
}


void fbcon_putc(struct vc_state *vt, int c, int x, int y)
{
    if (x < 0 || x >= vt->xs || y < 0 || y >= vt->ys)
        return;
    graphics_putc((u16)c, x, y);
}

void fbcon_putcs(struct vc_state *vt, const unsigned short *s, int count, int x, int y)
{

}


void fbcon_scroll(struct vc_state *vt, int top, int bottom, int dir, int lines)
{
    struct framebuffer *fb = vt->display_data;

    if (!fb || !fb->font || lines <= 0)
        return;

    if (dir == S_UP) {
        if (top + lines <= bottom)
            fbcon_move_chars(vt, top + lines, 0, top, 0, bottom - top - lines, vt->xs);
        fbcon_clear_region(vt, bottom - lines, 0, lines, vt->xs);
    } else if (dir == S_DOWN) {
        if (top + lines <= bottom)
            fbcon_move_chars(vt, top, 0, top + lines, 0, bottom - top - lines, vt->xs);
        fbcon_clear_region(vt, top, 0, lines, vt->xs);
    }
}


/**
 * Draw a rectangular cursor at the specified position
 * Inverts colors: fg pixels become bg, bg pixels become fg
 */
static void draw_cursor(struct vc_state *vt, int cx, int cy)
{
    struct framebuffer *fb = vt->display_data;

    if (!vt || !fb || !fb->fb || !fb->font)
        return;
    if (cx < 0 || cx >= vt->xs || cy < 0 || cy >= vt->ys)
        return;

    u32 offs = (cy * fb->font->height * fb->fb_pitch) +
        (cx * (fb->font->width + fb->width_pad) * sizeof(u32));

    for (int y = 0; y < fb->font->height; y++) {
        u32 line = offs;

        for (int x = 0; x < fb->font->width; x++) {
            u32 *pixel = (u32*)(fb->fb + line);
            u32 current_color = *pixel;

            if (current_color == fb->fb_fg)
                *pixel = fb->fb_bg;
            else if (current_color == fb->fb_bg)
                *pixel = fb->fb_fg;
            else
                *pixel = fb->fb_fg;

            line += sizeof(u32);
        }

        offs += fb->fb_pitch;
    }
}

void fbcon_cursor(struct vc_state *vc, int mode)
{
    draw_cursor(vc, vc->curx, vc->cury);
}

void fbcon_color(struct vc_state *vc, unsigned int fg, unsigned int bg)
{
    graphics_setcolor(fg, bg);
}

const struct con_display_ops fbcon_ops = {
    .con_init = fbcon_init,
    .con_clear = fbcon_clear_region,
    .con_putc = fbcon_putc,
    .con_scroll = fbcon_scroll,
    .con_bmove = fbcon_move_chars,
    .con_cursor = fbcon_cursor,
    .con_color = fbcon_color
};
