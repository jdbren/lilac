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


int fbcon_scroll(struct vc_state *conp, int t, int b, int dir, int count)
{
    struct framebuffer *fb = conp->display_data;

    if (!fb || !fb->fb || !fb->font)
        return 1;
    if (t < 0 || b < 0 || t >= conp->ys || b > conp->ys || t > b)
        return 1;
    if (count <= 0)
        return 0;
    else if (count > b - t + 1)
        count = b - t + 1;

    int char_width = fb->font->font->width;
    int char_height = fb->font->font->height;
    int bytes_per_pixel = 4; // 32-bit pixels

    // Calculate pixel dimensions - account for character spacing like graphics_putc
    int top_pixel = t * char_height;
    int scroll_height_pixels = (b - t + 1) * char_height;
    int scroll_lines_pixels = count * char_height;
    int width_pixels = conp->xs * (char_width + 1); // +1 for spacing

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
            int current_y = clear_start_y + y;

            // Clear each pixel in the line, respecting character boundaries
            for (int cx = 0; cx < conp->xs; cx++) {
                // Calculate character position like in graphics_putc
                u32 char_offs = (current_y * fb->fb_pitch) + (cx * (char_width + 1) * sizeof(u32));

                // Clear each pixel in the character cell
                for (int px = 0; px < char_width; px++) {
                    u32 *pixel = (u32*)(fb->fb + char_offs + (px * sizeof(u32)));
                    *pixel = fb->fb_bg;
                }
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
            int current_y = top_pixel + y;

            // Clear each pixel in the line, respecting character boundaries
            for (int cx = 0; cx < conp->xs; cx++) {
                // Calculate character position like in graphics_putc
                u32 char_offs = (current_y * fb->fb_pitch) + (cx * (char_width + 1) * sizeof(u32));

                // Clear each pixel in the character cell
                for (int px = 0; px < char_width; px++) {
                    u32 *pixel = (u32*)(fb->fb + char_offs + (px * sizeof(u32)));
                    *pixel = fb->fb_bg;
                }
            }
        }

    } else {
        return -EINVAL;
    }

    return 0; // Success
}


void draw_cursor(struct vc_state *vt, int x, int y);

/**
 * Clear cursor at the specified position by restoring original colors
 * This should be called before moving cursor to a new position
 * @param vt: virtual console state
 * @param x: cursor x position (in character coordinates)
 * @param y: cursor y position (in character coordinates)
 */
void clear_cursor(struct vc_state *vt, int x, int y)
{
    // Simply redraw the cursor at the same position to invert it back
    draw_cursor(vt, x, y);
}

/**
 * Put a pixel at the specified coordinates with the given color
 * @param screen: framebuffer structure
 * @param x: x coordinate in pixels
 * @param y: y coordinate in pixels
 * @param color: color value (0xRRGGBB format)
 */
static void put_pixel(struct framebuffer *screen, u32 x, u32 y, u32 color) {
    u32 where = x * sizeof(u32) + y * screen->fb_pitch;
    screen->fb[where] = color & 0xff; // BLUE
    screen->fb[where + 1] = (color >> 8) & 0xff; // GREEN
    screen->fb[where + 2] = (color >> 16) & 0xff; // RED
}

/**
 * Get a pixel color at the specified coordinates
 * @param screen: framebuffer structure
 * @param x: x coordinate in pixels
 * @param y: y coordinate in pixels
 * @return: color value in 0xRRGGBB format
 */
static u32 get_pixel(struct framebuffer *screen, u32 x, u32 y) {
    u32 where = x * sizeof(u32) + y * screen->fb_pitch;
    u32 blue = screen->fb[where];
    u32 green = screen->fb[where + 1];
    u32 red = screen->fb[where + 2];
    return (red << 16) | (green << 8) | blue;
}

/**
 * Draw a rectangular cursor at the specified position
 * Inverts colors: fg pixels become bg, bg pixels become fg
 * @param vt: virtual console state
 * @param cx: cursor x position (in character coordinates)
 * @param cy: cursor y position (in character coordinates)
 */
void draw_cursor(struct vc_state *vt, int cx, int cy)
{
    struct framebuffer *fb = vt->display_data;

    if (!vt || !fb || !fb->fb || !fb->font) {
        return;
    }

    // Get font dimensions - assuming same structure as your font
    // Adjust these based on your actual font structure
    int font_width = fb->font->font->width;
    int font_height = fb->font->font->height;

    // Calculate console dimensions
    int console_width_chars = fb->fb_width / (font_width + 1); // +1 for spacing like in graphics_putc
    int console_height_chars = fb->fb_height / font_height;

    // Bounds checking
    if (cx < 0 || cx >= console_width_chars || cy < 0 || cy >= console_height_chars) {
        return;
    }

    // Calculate starting position - matching graphics_putc coordinate calculation
    u32 offs = (cy * font_height * fb->fb_pitch) + (cx * (font_width + 1) * sizeof(u32));

    // Draw the cursor by inverting colors in the character cell
    for (int y = 0; y < font_height; y++) {
        u32 line = offs;

        for (int x = 0; x < font_width; x++) {
            // Get current pixel color
            u32 *pixel = (u32*)(fb->fb + line);
            u32 current_color = *pixel;

            // Invert the color: if it matches fg, make it bg; if it matches bg, make it fg
            if (current_color == fb->fb_fg) {
                *pixel = fb->fb_bg;
            } else if (current_color == fb->fb_bg) {
                *pixel = fb->fb_fg;
            } else {
                // For other colors, default to making it foreground color
                *pixel = fb->fb_fg;
            }

            // Move to next pixel
            line += sizeof(u32);
        }

        // Move to next line
        offs += fb->fb_pitch;
    }
}

void set_cursor(struct vc_state *vt, int x, int y)
{
    draw_cursor(vt, x, y);
}

const struct con_display_ops fbcon_ops = {
    .con_clear = fbcon_clear,
    .con_putc = fbcon_putc,
    .con_scroll = fbcon_scroll
};
