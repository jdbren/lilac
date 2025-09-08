#include <lilac/console.h>
#include <lilac/libc.h>
#include <lilac/tty.h>
#include <lilac/err.h>
#include <drivers/framebuffer.h>

extern struct framebuffer *fb;

void fbcon_init(struct vc_state *vc, int)
{
    vc->display_data = fb;
    vc->screen_buf = fb->fb;
}


static inline u8* fb_get_char_pixel(struct framebuffer *fb, int row, int col, int pixel_y)
{
    int y = row * fb->font->height + pixel_y;
    int x = col * fb->font->width;
    return fb->fb + (y * fb->fb_pitch) + (x * 4);
}

static void fb_clear_rect_pixels(struct framebuffer *fb, int x, int y, int width, int height)
{
    for (int py = 0; py < height; py++) {
        u8 *row = fb->fb + ((y + py) * fb->fb_pitch) + (x * 4);
        for (int px = 0; px < width; px++) {
            *(u32 *)(row + (px * 4)) = fb->fb_bg;
        }
    }
}

/**
 * Clear a rectangular region of characters
 * @param fb: framebuffer structure
 * @param row: starting character row
 * @param col: starting character column
 * @param height: height in characters
 * @param width: width in characters
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
 * @param fb: framebuffer structure
 * @param src_row: source row
 * @param src_col: source column
 * @param dst_row: destination row
 * @param dst_col: destination column
 * @param height: height in characters to move
 * @param width: width in characters to move
 *
 * Note: This function handles both horizontal and vertical shifts.
 * For scrolling, set appropriate src/dst rows with full width.
 * For horizontal shifts within a line, set same src/dst rows with different columns.
 */
void fbcon_move_chars(struct vc_state *vc,
                     int src_row, int src_col,
                     int dst_row, int dst_col,
                     int height, int width)
{
    struct framebuffer *fb = vc->display_data;
    if (!fb || !fb->fb || !fb->font || height <= 0 || width <= 0)
        return;

    int char_h = fb->font->height;
    bool copy_backwards = (dst_row > src_row) ||
                         (dst_row == src_row && dst_col > src_col);

    if (copy_backwards) {
        // Copy from bottom-right to top-left
        for (int row = height - 1; row >= 0; row--) {
            for (int py = char_h - 1; py >= 0; py--) {
                u8 *src = fb_get_char_pixel(fb, src_row + row, src_col, py);
                u8 *dst = fb_get_char_pixel(fb, dst_row + row, dst_col, py);
                memmove(dst, src, fb->fb_pitch);
            }
        }
    } else {
        // Copy from top-left to bottom-right
        for (int row = 0; row < height; row++) {
            for (int py = 0; py < char_h; py++) {
                u8 *src = fb_get_char_pixel(fb, src_row + row, src_col, py);
                u8 *dst = fb_get_char_pixel(fb, dst_row + row, dst_col, py);
                memmove(dst, src, fb->fb_pitch);
            }
        }
    }

    if ((src_row != dst_row || src_col != dst_col)) {
        fbcon_clear_region(vc, src_row, src_col, height, width);
    }
}

/*
 * Example usage patterns:
 *
 * 1. Clear entire line:
 *    fbcon_clear_region(fb, row, 0, 1, console_width);
 *
 * 2. Clear from cursor to end of line:
 *    fbcon_clear_region(fb, row, col, 1, console_width - col);
 *
 * 3. Scroll region up by one line:
 *    fbcon_move_chars(fb, top+1, 0, top, 0, bottom-top, console_width, false);
 *    fbcon_clear_region(fb, bottom, 0, 1, console_width);
 *
 * 4. Insert characters (shift right):
 *    fbcon_move_chars(fb, row, col, row, col+n, 1, console_width-col-n, true);
 *
 * 5. Delete characters (shift left):
 *    fbcon_move_chars(fb, row, col+n, row, col, 1, console_width-col-n, true);
 */


void fbcon_putc(struct vc_state *vt, int c, int x, int y)
{
    if (x < 0 || x >= vt->xs || y < 0 || y >= vt->ys)
        return;
    graphics_putc((u16)c, x, y);
}

void fbcon_putcs(struct vc_state *vt, const unsigned short *s, int count, int x, int y)
{

}


static
void clear_region(struct vc_state *conp, int start_y, int lines,
    int char_width, int char_height)
{
    struct framebuffer *fb = conp->display_data;
    for (int y = 0; y < lines; y++) {
        int current_y = start_y + y;
        for (int cx = 0; cx < conp->xs; cx++) {
            u32 char_offs = (current_y * fb->fb_pitch) +
                            (cx * (char_width + 1) * sizeof(u32));
            for (int px = 0; px < char_width; px++) {
                u32 *pixel = (u32 *)(fb->fb + char_offs + (px * sizeof(u32)));
                *pixel = fb->fb_bg;
            }
        }
    }
}


int fbcon_scroll(struct vc_state *conp, int t, int b, int dir, int count)
{
    struct framebuffer *fb = conp->display_data;

    if (!fb || !fb->fb || !fb->font)
        return 1;
    if (t < 0 || b < 0 || t >= conp->ys || b > conp->ys || t > b)
        return 1;
    if (count <= 0)
        return 0;
    if (count > b - t + 1)
        count = b - t + 1;

    int char_w = fb->font->width;
    int char_h = fb->font->height;

    int top_px      = t * char_h;
    int total_px    = (b - t + 1) * char_h;
    int scroll_px   = count * char_h;
    int width_px    = conp->xs * (char_w + 1);
    int move_height = total_px - scroll_px;

    if (dir == S_UP) {
        for (int y = 0; y < move_height; y++) {
            u8 *src_row = fb->fb + ((top_px + scroll_px + y) * fb->fb_pitch);
            u8 *dst_row = fb->fb + ((top_px + y) * fb->fb_pitch);
            memmove(dst_row, src_row, width_px * fb->bypp);
        }
        clear_region(conp, top_px + move_height, scroll_px, char_w, char_h);
    } else if (dir == S_DOWN) {
        for (int y = move_height - 1; y >= 0; y--) {
            u8 *src_row = fb->fb + ((top_px + y) * fb->fb_pitch);
            u8 *dst_row = fb->fb + ((top_px + scroll_px + y) * fb->fb_pitch);
            memmove(dst_row, src_row, width_px * fb->bypp);
        }
        clear_region(conp, top_px, scroll_px, char_w, char_h);
    } else {
        return -EINVAL;
    }

    return 0;
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

    // Calculate starting position - matching graphics_putc coordinate calculation
    u32 offs = (cy * fb->font->height * fb->fb_pitch) + (cx * (fb->font->width + 1) * sizeof(u32));

    for (int y = 0; y < fb->font->height; y++) {
        u32 line = offs;

        for (int x = 0; x < fb->font->width; x++) {
            u32 *pixel = (u32*)(fb->fb + line);
            u32 current_color = *pixel;

            if (current_color == fb->fb_fg) {
                *pixel = fb->fb_bg;
            } else if (current_color == fb->fb_bg) {
                *pixel = fb->fb_fg;
            } else {
                *pixel = fb->fb_fg;
            }

            line += sizeof(u32);
        }

        offs += fb->fb_pitch;
    }
}

void display_cursor(struct vc_state *vt, int x, int y)
{
    draw_cursor(vt, x, y);
}

void clear_cursor(struct vc_state *vt, int x, int y)
{
    draw_cursor(vt, x, y);
}

const struct con_display_ops fbcon_ops = {
    .con_init = fbcon_init,
    .con_clear = fbcon_clear_region,
    .con_putc = fbcon_putc,
    .con_scroll = fbcon_scroll,
    .con_bmove = fbcon_move_chars
};
