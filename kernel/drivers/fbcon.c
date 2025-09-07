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

void fbcon_clear(struct vc_state *vc, int sy, int sx, int height, int width)
{
    struct framebuffer *fb = vc->display_data;

    if (!fb || !fb->fb)
        return;

    // conversion to px
    sx *= fb->font->width;
    sy *= fb->font->height;
    width *= fb->font->width;
    height *= fb->font->height;

    if (sx >= fb->fb_width || sy >= fb->fb_height)
        return;

    if (sx + width > fb->fb_width)
        width = fb->fb_width - sx;
    if (sy + height > fb->fb_height)
        height = fb->fb_height - sy;

    for (int y = 0; y < height; y++) {
        u8 *row = fb->fb + ((sy + y) * fb->fb_pitch) + (sx * fb->bypp);

        for (int x = 0; x < width; x++) {
            u32 *pixel = (u32 *)(row + (x * fb->bypp));
            *pixel = fb->fb_bg;
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
        clear_region( conp, top_px, scroll_px, char_w, char_h);
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
    .con_clear = fbcon_clear,
    .con_putc = fbcon_putc,
    .con_scroll = fbcon_scroll
};
