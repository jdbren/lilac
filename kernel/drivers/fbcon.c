#include <lilac/console.h>
#include <lilac/libc.h>
#include <lilac/tty.h>
#include <lilac/err.h>
#include <drivers/framebuffer.h>


void fbcon_clear(struct vc_state *vc, int sy, int sx, int height, int width)
{
    struct framebuffer *fb = vc->display_data;

    if (!fb || !fb->fb)
        return;

    int char_width = fb->font->font->width;
    int char_height = fb->font->font->height;
    int start_x = sx * char_width;
    int start_y = sy * char_height;
    int pixel_width = width * char_width;
    int pixel_height = height * char_height;

    if (start_x >= fb->fb_width || start_y >= fb->fb_height)
        return;

    // Clamp dimensions to framebuffer bounds
    if (start_x + pixel_width > fb->fb_width) {
        pixel_width = fb->fb_width - start_x;
    }
    if (start_y + pixel_height > fb->fb_height) {
        pixel_height = fb->fb_height - start_y;
    }

    int bytes_per_pixel = 4;
    for (int y = 0; y < pixel_height; y++) {
        u8 *row = fb->fb + ((start_y + y) * fb->fb_pitch) + (start_x * bytes_per_pixel);

        for (int x = 0; x < pixel_width; x++) {
            u32 *pixel = (u32 *)(row + (x * bytes_per_pixel));
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

void fbcon_cursor(struct vc_state *vt, int on)
{
    vt->vt_cursor = !!on;
}

int fbcon_scroll(struct vc_state *conp, int t, int b, int dir, int count)
{
    struct framebuffer *fb = conp->display_data;

    if (!fb || !fb->fb || !fb->font)
        return 1;
    if (t < 0 || b < 0 || t >= conp->ys || b >= conp->ys || t > b)
        return 1;

    if (count <= 0)
        return 0;
    else if (count > b - t + 1)
        count = b - t + 1;

    int char_width = fb->font->font->width;
    int char_height = fb->font->font->height;
    int bytes_per_pixel = 4; // Assume 32-bit RGBA
    int top_pixel = t * char_height;
    int scroll_height_pixels = (b - t + 1) * char_height;
    int scroll_lines_pixels = count * char_height;
    int width_pixels = conp->xs * char_width;

    if (dir == S_UP) {
        int src_y = top_pixel + scroll_lines_pixels;
        int dst_y = top_pixel;
        int move_height = scroll_height_pixels - scroll_lines_pixels;

        if (move_height > 0) {
            // Move existing content up
            for (int y = 0; y < move_height; y++) {
                u8 *src_row = fb->fb + ((src_y + y) * fb->fb_pitch);
                u8 *dst_row = fb->fb + ((dst_y + y) * fb->fb_pitch);

                // Use memmove for overlapping memory regions
                memmove(dst_row, src_row, width_pixels * bytes_per_pixel);
            }
        }

        // Clear the bottom region that was exposed
        int clear_start_y = top_pixel + move_height;
        for (int y = 0; y < scroll_lines_pixels; y++) {
            u8 *row = fb->fb + ((clear_start_y + y) * fb->fb_pitch);

            for (int x = 0; x < width_pixels; x++) {
                u32 *pixel = (u32 *)(row + (x * bytes_per_pixel));
                *pixel = fb->fb_bg;
            }
        }

    } else if (dir == S_DOWN) {
        int move_height = scroll_height_pixels - scroll_lines_pixels;

        if (move_height > 0) {
            // Move existing content down (start from bottom to avoid overlap issues)
            for (int y = move_height - 1; y >= 0; y--) {
                u8 *src_row = fb->fb + ((top_pixel + y) * fb->fb_pitch);
                u8 *dst_row = fb->fb + ((top_pixel + scroll_lines_pixels + y) * fb->fb_pitch);

                memmove(dst_row, src_row, width_pixels * bytes_per_pixel);
            }
        }

        // Clear the top region that was exposed
        for (int y = 0; y < scroll_lines_pixels; y++) {
            u8 *row = fb->fb + ((top_pixel + y) * fb->fb_pitch);

            for (int x = 0; x < width_pixels; x++) {
                u32 *pixel = (u32 *)(row + (x * bytes_per_pixel));
                *pixel = fb->fb_bg;
            }
        }

    } else {
        return -EINVAL;
    }

    return 0; // Success
}


const struct con_display_ops fbcon_ops = {
    .con_clear = fbcon_clear,
    .con_putc = fbcon_putc,
    .con_scroll = fbcon_scroll
};
