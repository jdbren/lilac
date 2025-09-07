/*
 * This file has been substantially modified, but the logic is taken from
 * Minicom source code (vt100.c). Original header below.
 *
 * vt100.c	ANSI/VT102 emulator code.
 *		This code was integrated to the Minicom communications
 *		package, but has been reworked to allow usage as a separate
 *		module.
 *
 *		It's not a "real" vt102 emulator - it's more than that:
 *		somewhere between vt220 and vt420 in commands.
 *
 *		This file is part of the minicom communications package,
 *		Copyright 1991-1995 Miquel van Smoorenburg.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * // jl 04.09.97 character map conversions in and out
 *    jl 06.07.98 use conversion tables with the capture file
 *    mark.einon@gmail.com 16/02/11 - Added option to timestamp terminal output
 */
#pragma GCC diagnostic ignored "-Wunterminated-string-initialization"
#include "vt100.h"

#include <lilac/types.h>
#include <lilac/libc.h>
#include <lilac/time.h>
#include <lilac/timer.h>
#include <lilac/tty.h>
#include <lilac/console.h>
#include <drivers/framebuffer.h>

/*
 * The variable vt->esc_s holds the escape sequence status:
 * 0 - normal
 * 1 - ESC
 * 2 - ESC [
 * 3 - ESC [ ?
 * 4 - ESC (
 * 5 - ESC )
 * 6 - ESC #
 * 7 - ESC P
 * 8 - ESC ]
 */

#define ESC 27

/* Structure to hold escape sequences. */
struct escseq {
  int code;
  const char *vt100_st;
  const char *vt100_app;
  const char *ansi;
};

/* Escape sequences for different terminal types. */
static struct escseq vt_keys[] = {
  { K_F1,	"OP",	"OP",	"OP" },
  { K_F2,	"OQ",	"OQ",	"OQ" },
  { K_F3,	"OR",	"OR",	"OR" },
  { K_F4,	"OS",	"OS",	"OS" },
  { K_F5,	"[16~",	"[16~",	"OT" },
  { K_F6,	"[17~",	"[17~",	"OU" },
  { K_F7,	"[18~",	"[18~",	"OV" },
  { K_F8,	"[19~",	"[19~",	"OW" },
  { K_F9,	"[20~",	"[20~",	"OX" },
  { K_F10,	"[21~",	"[21~",	"OY" },
  { K_F11,	"[23~",	"[23~",	"OY" },
  { K_F12,	"[24~",	"[24~",	"OY" },
  { K_HOME,	"[1~",	"[1~",	"[H" },
  { K_PGUP,	"[5~",	"[5~",	"[V" },
  { K_UP,	"[A",	"OA",	"[A" },
  { K_LT,	"[D",	"OD",	"[D" },
  { K_RT,	"[C",	"OC",	"[C" },
  { K_DN,	"[B",	"OB",	"[B" },
  { K_END,	"[4~",	"[4~",	"[Y" },
  { K_PGDN,	"[6~",	"[6~",	"[U" },
  { K_INS,	"[2~",	"[2~",	"[@" },
  { K_DEL,	"[3~",	"[3~",	"\177" },
  { 0,		NULL,	NULL,	NULL }
};

/* Two tables for user-defined character map conversions.
 * defmap.h should contain all characters 0-255 in ascending order
 * to default to no conversion.    jl 04.09.1997
 */

unsigned char vt_inmap[256] = {
#include "defmap.h"
};
unsigned char vt_outmap[256] = {
#include "defmap.h"
};

/* Taken from the Linux kernel source: linux/drivers/char/console.c */
static char * vt_map[] = {
/* 8-bit Latin-1 mapped to the PC character set: '.' means non-printable */
  "................"
  "................"
  " !\"#$%&'()*+,-./0123456789:;<=>?"
  "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
  "`abcdefghijklmnopqrstuvwxyz{|}~."
  "................"
  "................"
  "\377\255\233\234\376\235\174\025\376\376\246\256\252\055\376\376"
  "\370\361\375\376\376\346\024\371\376\376\247\257\254\253\376\250"
  "\376\376\376\376\216\217\222\200\376\220\376\376\376\376\376\376"
  "\376\245\376\376\376\376\231\376\350\376\376\376\232\376\376\341"
  "\205\240\203\376\204\206\221\207\212\202\210\211\215\241\214\213"
  "\376\244\225\242\223\376\224\366\355\227\243\226\201\376\376\230",
/* vt100 graphics */
  "................"
  "\0\0\0\0\0\0\0\0\0\0\376\0\0\0\0\0"
  " !\"#$%&'()*+,-./0123456789:;<=>?"
  "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^ "
  "\004\261\007\007\007\007\370\361\007\007\331\277\332\300\305\304"
  "\304\304\137\137\303\264\301\302\263\363\362\343\330\234\007\0"
  "................"
  "................"
  "\377\255\233\234\376\235\174\025\376\376\246\256\252\055\376\376"
  "\370\361\375\376\376\346\024\371\376\376\247\257\254\253\376\250"
  "\376\376\376\376\216\217\222\200\376\220\376\376\376\376\376\376"
  "\376\245\376\376\376\376\231\376\376\376\376\376\232\376\376\341"
  "\205\240\203\376\204\206\221\207\212\202\210\211\215\241\214\213"
  "\376\244\225\242\223\376\224\366\376\227\243\226\201\376\376\230"
};

#define mc_wgetattr(w) ( (w)->attr )
#define wsetattr(w,a) ( (w)->attr = (a) )

int vt_open(struct tty *tty, struct file *file);
void vt_close(struct tty *tty, struct file *f);
ssize_t vt_write(struct tty *tty, const u8 *buf, size_t count);

static void v_termout(struct vc_state *vt, const char *s, int len);

const struct tty_operations vt_tty_ops = {
    .open = vt_open,
    .close = vt_close,
    .write = vt_write,
};

static struct vc_state vt_cons[8] = {
    [0 ... 7] = {
        .con_ops = &fbcon_ops,
        .vt_echo = 1,
        .vt_wrap = 1,
        .vt_addlf = 0,
        .vt_addcr = 1,
        .vt_keypad = 0,
        .vt_cursor_on = 1,
        .vt_asis = 0,
        .vt_insert = 0,
        .vt_crlf = 0,
        .vt_om = 0,
        .vt_doscroll = 1,
        .xs = 80,
        .ys = 24,
        .scroll_top = 0,
        .scroll_bottom = 24,
        .vt_tabs = { [0] = 0x01010100, [1 ... 4] = 0x01010101 },
        .vt_fg = WHITE,
        .vt_bg = BLACK,
        .attr = XA_NORMAL,
        .curx = 0,
        .cury = 0,
        .save_fg = WHITE
    }
};


int vt_open(struct tty *tty, struct file *file)
{
    struct vc_state *vt = &vt_cons[tty->index];
    if (!tty->driver_data) {
        vt->con_ops->con_init(vt, 0);
        tty->driver_data = &vt_cons[tty->index];
    }
    vt->con_ops->con_clear(vt, 0, 0, vt->ys, vt->xs);
    return 0;
}


/*
* Control chars
*/

static void lf(struct vc_state *vc)
{
    /* don't scroll if above bottom of scrolling region, or
    * if below scrolling region
    */
    if (vc->cury + 1 == vc->scroll_bottom)
        vc->con_ops->con_scroll(vc, vc->scroll_top, vc->scroll_bottom, S_UP, 1);
    else if (vc->cury < vc->ys - 1)
        vc->cury++;
}

static void ri(struct vc_state *vc)
{
    /* don't scroll if below top of scrolling region, or
    * if above scrolling region
    */
    if (vc->cury == vc->scroll_top)
        vc->con_ops->con_scroll(vc, vc->scroll_top, vc->scroll_bottom, S_DOWN, 1);
    else if (vc->cury > 0)
        vc->cury--;
}

static void cr(struct vc_state *vc)
{
    vc->curx = 0;
}

static void bs(struct vc_state *vc)
{
    if (vc->curx)
        vc->curx--;
}

static void del(struct vc_state *vc)
{
    /* ignored */
}


/*
* Cursor and scroll settings
*/

static void wredraw(struct vc_state *vt, int i)
{
}

// Move cursor to (x,y)
static void locate_curs(struct vc_state *v, int x, int y)
{
    if (x < 0) x = 0;
    if (x >= v->xs) x = v->xs - 1;
    if (y < 0) y = 0;
    if (y >= v->ys) y = v->ys - 1;
    v->curx = x;
    v->cury = y;
}

// Set scroll region (DECSTBM)
static void set_scrl_reg(struct vc_state *vt, int top, int bottom)
{
    if (top < 0) top = 0;
    if (bottom >= vt->ys) bottom = vt->ys - 1;
    if (top >= bottom) {
        top = 0;
        bottom = vt->ys - 1;
    }
    vt->scroll_top = top;
    vt->scroll_bottom = bottom;
}

static inline void wsetfgcol(struct vc_state *vt, int col)
{
    // graphics_setcolor()
    vt->vt_fg = col;
}

static inline void wsetbgcol(struct vc_state *vt, int col)
{
    // graphics_setcolor()
    vt->vt_bg = col;
}


/*
* Line clear operations
*/

// Clear a single line completely
static inline void wclrch(struct vc_state *vt, int y)
{
    for (int x = 0; x < vt->xs; x++)
        vt->con_ops->con_putc(vt, ' ', x, y);
}

// Clear from cursor → end of line
static inline void wclreol(struct vc_state *vt)
{
    for (int x = vt->curx; x < vt->xs; x++)
        vt->con_ops->con_putc(vt, ' ', x, vt->cury);
}

// Clear from beginning of line → cursor
static inline void wclrel(struct vc_state *vt)
{
    for (int x = 0; x <= vt->curx; x++)
        vt->con_ops->con_putc(vt, ' ', x, vt->cury);
}

// Clear from cursor → end of screen
static inline void wclreos(struct vc_state *vt)
{
    wclreol(vt);
    for (int y = vt->cury + 1; y < vt->ys; y++)
        wclrch(vt, y);
}

// Clear from beginning of screen → cursor
static inline void wclrbos(struct vc_state *vt)
{
    for (int y = 0; y < vt->cury; y++)
        wclrch(vt, y);
    wclrel(vt);
}

// Clear entire screen
static inline void winclr(struct vc_state *vt)
{
    vt->con_ops->con_clear(vt, 0, 0, vt->ys, vt->xs);
    vt->curx = 0;
    vt->cury = 0;
}

// scroll one line up or down
static inline void wscroll(struct vc_state *v, int dir)
{
    v->con_ops->con_scroll(v, v->scroll_top, v->scroll_bottom, dir, 1);
}


/*
 * Print a character in a window.
 */
void wputc(struct vc_state *vt, char c)
{
    /* See if we need to scroll/move. (vt100 behaviour!) */
    if (c == '\n' || (vt->curx >= vt->xs && vt->vt_wrap)) {
        if (c != '\n')
            vt->curx = 0;
        vt->cury++;
        if (vt->cury >= vt->ys - 1) {
            if (vt->vt_doscroll)
                wscroll(vt, S_UP);
        }
        if (vt->cury >= vt->ys)
            vt->cury = vt->ys - 1;
    }
    /* Now write the character. */
    if (c != '\n') {
        if (!vt->vt_wrap && vt->curx >= vt->xs)
            c = '>';
        vt->con_ops->con_putc(vt, c, vt->curx, vt->cury);
        if (++vt->curx >= vt->xs && !vt->vt_wrap)
            vt->curx = 0; /* Force to move */
    }
}

/**
 * Shift characters horizontally in the framebuffer
 * @param fb: framebuffer structure
 * @param row: character row to shift
 * @param start_col: starting column for the shift
 * @param shift_count: number of character positions to shift (positive = right, negative = left)
 * @param total_cols: total number of columns in the console
 */
static void fb_shift_chars_horizontal(struct framebuffer *fb, int row, int start_col,
                                    int shift_count, int total_cols)
{
    if (!fb || !fb->fb || !fb->font || shift_count == 0) {
        return;
    }

    int char_width = fb->font->font->width;
    int char_height = fb->font->font->height;
    int bytes_per_pixel = 4; // Assume 32-bit RGBA

    // Convert to pixel coordinates
    int row_start_pixel = row * char_height;
    int start_col_pixel = start_col * char_width;
    int char_width_bytes = char_width * bytes_per_pixel;

    // Calculate how many characters we can actually shift
    int chars_to_shift;
    if (shift_count > 0) {
        // Shifting right - limit by remaining space
        chars_to_shift = total_cols - start_col - shift_count;
        if (chars_to_shift <= 0) {
            return; // Nothing to shift
        }
    } else {
        // Shifting left - limit by available characters
        chars_to_shift = total_cols - start_col;
        if (chars_to_shift <= 0) {
            return; // Nothing to shift
        }
    }

    // Perform the shift for each row of pixels in the character
    for (int y = 0; y < char_height; y++) {
        u8 *row_base = fb->fb + ((row_start_pixel + y) * fb->fb_pitch);

        if (shift_count > 0) {
            // Shift right - work backwards to avoid overwriting
            for (int col = chars_to_shift - 1; col >= 0; col--) {
                u8 *src = row_base + ((start_col + col) * char_width_bytes);
                u8 *dst = row_base + ((start_col + col + shift_count) * char_width_bytes);
                memmove(dst, src, char_width_bytes);
            }
        } else {
            // Shift left - work forwards
            int abs_shift = -shift_count;
            for (int col = 0; col < chars_to_shift - abs_shift; col++) {
                u8 *src = row_base + ((start_col + col + abs_shift) * char_width_bytes);
                u8 *dst = row_base + ((start_col + col) * char_width_bytes);
                memmove(dst, src, char_width_bytes);
            }
        }
    }
}

/**
 * Clear character positions in the framebuffer
 * @param fb: framebuffer structure
 * @param row: character row to clear
 * @param start_col: starting column to clear
 * @param count: number of characters to clear
 */
static void fb_clear_chars(struct framebuffer *fb, int row, int start_col, int count)
{
    if (!fb || !fb->fb || !fb->font || count <= 0)
        return;

    int char_width = fb->font->font->width;
    int char_height = fb->font->font->height;
    int bytes_per_pixel = 4;

    // Convert to pixel coordinates
    int row_start_pixel = row * char_height;
    int start_col_pixel = start_col * char_width;
    int clear_width_pixels = count * char_width;

    // Clear each row of pixels
    for (int y = 0; y < char_height; y++) {
        u8 *row_ptr = fb->fb + ((row_start_pixel + y) * fb->fb_pitch) + (start_col_pixel * bytes_per_pixel);

        for (int x = 0; x < clear_width_pixels; x++) {
            u32 *pixel = (u32 *)(row_ptr + (x * bytes_per_pixel));
            *pixel = fb->fb_bg;
        }
    }
}

/**
 * Insert characters at the current cursor position
 */
void winsnchar(struct vc_state *v, char c, int count)
{
    struct framebuffer *fb = v->display_data;

    if (!v || !fb || count <= 0)
        return;

    int console_width_chars = fb->fb_width / fb->font->font->width;
    int cur_x = v->curx;
    int cur_y = v->cury;

    if (cur_x >= console_width_chars || cur_y < 0)
        return;

    int available_space = console_width_chars - cur_x;
    if (count > available_space)
        count = available_space;

    // Shift existing characters to the right to make room
    if (cur_x + count < console_width_chars)
        fb_shift_chars_horizontal(fb, cur_y, cur_x, count, console_width_chars);

    // Clear the positions where we'll insert new characters
    fb_clear_chars(fb, cur_y, cur_x, count);

    // Insert the new characters using wputc
    int original_x = v->curx;
    for (int i = 0; i < count; i++) {
        v->curx = original_x + i;
        wputc(v, c);
    }

    // Restore cursor to original position (some implementations expect this)
    v->curx = original_x;
}


/**
 * Shift lines vertically in the framebuffer within a scroll region
 * @param fb: framebuffer structure
 * @param top_line: top of scroll region (inclusive)
 * @param bottom_line: bottom of scroll region (inclusive)
 * @param start_line: line where shift starts
 * @param shift_count: number of lines to shift (positive = down, negative = up)
 */
static void fb_shift_lines_vertical(struct framebuffer *fb, int top_line, int bottom_line,
                                  int start_line, int shift_count)
{
    if (!fb || !fb->fb || !fb->font || shift_count == 0) {
        return;
    }

    int char_height = fb->font->font->height;
    int char_width = fb->font->font->width;
    int console_width_chars = fb->fb_width / char_width;
    int line_width_bytes = console_width_chars * char_width * 4; // 4 bytes per pixel

    // Convert to pixel coordinates
    int top_pixel = top_line * char_height;
    int bottom_pixel = (bottom_line + 1) * char_height - 1;
    int start_pixel = start_line * char_height;

    if (shift_count > 0) {
        // Shifting down - work from bottom to top to avoid overwriting
        int lines_to_move = bottom_line - start_line + 1 - shift_count;
        if (lines_to_move <= 0) {
            return; // Nothing to move
        }

        for (int line = lines_to_move - 1; line >= 0; line--) {
            int src_line = start_line + line;
            int dst_line = start_line + line + shift_count;

            // Move each pixel row of the character line
            for (int y = 0; y < char_height; y++) {
                u8 *src_row = fb->fb + ((src_line * char_height + y) * fb->fb_pitch);
                u8 *dst_row = fb->fb + ((dst_line * char_height + y) * fb->fb_pitch);
                memmove(dst_row, src_row, line_width_bytes);
            }
        }
    } else {
        // Shifting up - work from top to bottom
        int abs_shift = -shift_count;
        int lines_to_move = bottom_line - start_line + 1 - abs_shift;
        if (lines_to_move <= 0) {
            return; // Nothing to move
        }

        for (int line = 0; line < lines_to_move; line++) {
            int src_line = start_line + line + abs_shift;
            int dst_line = start_line + line;

            // Move each pixel row of the character line
            for (int y = 0; y < char_height; y++) {
                u8 *src_row = fb->fb + ((src_line * char_height + y) * fb->fb_pitch);
                u8 *dst_row = fb->fb + ((dst_line * char_height + y) * fb->fb_pitch);
                memmove(dst_row, src_row, line_width_bytes);
            }
        }
    }
}

/**
 * Clear complete lines in the framebuffer
 * @param fb: framebuffer structure
 * @param start_line: starting line to clear
 * @param line_count: number of lines to clear
 */
static void fb_clear_lines(struct framebuffer *fb, int start_line, int line_count)
{
    if (!fb || !fb->fb || !fb->font || line_count <= 0)
        return;

    int char_height = fb->font->font->height;
    int char_width = fb->font->font->width;
    int console_width_chars = fb->fb_width / char_width;
    int bytes_per_pixel = 4;

    // Clear each line
    for (int line = 0; line < line_count; line++) {
        int current_line = start_line + line;

        // Clear each pixel row in the character line
        for (int y = 0; y < char_height; y++) {
            u8 *row = fb->fb + ((current_line * char_height + y) * fb->fb_pitch);

            for (int x = 0; x < console_width_chars * char_width; x++) {
                u32 *pixel = (u32 *)(row + (x * bytes_per_pixel));
                *pixel = fb->fb_bg;
            }
        }
    }
}

/**
 * Insert a blank line at the current cursor position
 * Lines below are pushed down, bottom line in scroll region is lost
 * @param vt: virtual console state
 */
void mc_winsline(struct vc_state *vt)
{
    struct framebuffer *fb = vt->display_data;

    if (!vt || !fb || !fb->font) {
        return;
    }

    int char_height = fb->font->font->height;
    int console_height_chars = fb->fb_height / char_height;

    int cur_y = vt->cury;

    // Bounds checking
    if (cur_y < 0 || cur_y >= console_height_chars) {
        return;
    }

    // If we have explicit scroll region support in vt, use it:
    int scroll_top = vt->scroll_top;
    int scroll_bottom = vt->scroll_bottom;

    // Only operate within scroll region
    if (cur_y < scroll_top || cur_y > scroll_bottom)
        return;

    // Shift lines down from current position to make room
    if (cur_y < scroll_bottom)
        fb_shift_lines_vertical(fb, scroll_top, scroll_bottom, cur_y, 1);

    // Clear the current line
    fb_clear_lines(fb, cur_y, 1);
}

/**
 * Delete the current line
 * Lines below are pulled up, blank line appears at bottom of scroll region
 * @param vt: virtual console state
 */
void mc_wdelline(struct vc_state *vt)
{
    if (!vt || !vt->con_ops || !vt->con_ops->con_scroll) {
        return;
    }

    int cur_y = vt->cury;

    // Bounds checking - ensure cursor is within scroll region
    if (cur_y < vt->scroll_top || cur_y > vt->scroll_bottom) {
        return;
    }

    // Temporarily adjust scroll region to start from cursor position + 1
    int old_scroll_top = vt->scroll_top;
    vt->scroll_top = cur_y + 1;

    // Scroll up (dir=1) to pull lines up and fill the deleted line
    if (vt->scroll_top <= vt->scroll_bottom)
        vt->con_ops->con_scroll(vt, vt->scroll_top, vt->scroll_bottom, 1, 1);

    // Restore original scroll region
    vt->scroll_top = old_scroll_top;

    // Clear the bottom line of the original scroll region
    struct framebuffer *fb = vt->display_data;
    if (fb)
        fb_clear_lines(fb, vt->scroll_bottom, 1);
}

/**
 * Delete character at current cursor position
 * Characters to the right are pulled left, space appears at end of line
 * @param vt: virtual console state
 */
void mc_wdelchar(struct vc_state *vt)
{
    struct framebuffer *fb = vt->display_data;

    if (!vt || !fb || !fb->font)
        return;

    int char_width = fb->font->font->width;
    int console_width_chars = fb->fb_width / char_width;

    int cur_x = vt->curx;
    int cur_y = vt->cury;

    // Bounds checking
    if (cur_x < 0 || cur_x >= console_width_chars || cur_y < 0)
        return;

    // Shift characters left to fill the deleted character position
    if (cur_x < console_width_chars - 1)
        fb_shift_chars_horizontal(fb, cur_y, cur_x + 1, -1, console_width_chars);

    // Clear the last character position on the line
    fb_clear_chars(fb, cur_y, console_width_chars - 1, 1);
}

// Insert blank char at cursor, shift rest of line right
void winschar(struct vc_state *vt)
{
    winsnchar(vt, ' ', 1);
}

// Write a null-terminated string to the VT
void wputs(struct vc_state *v, const char *s)
{
    while (*s)
        wputc(v, *s++);
}



/*
 * Escape code handling.
 */

/*
 * ESC was seen the last time. Process the next character.
 */
static void state1(struct vc_state *vt, int c)
{
    short x, y, f;

    switch(c) {
    case '[': /* ESC [ */
        vt->esc_s = 2;
        return;
    case '(': /* ESC ( */
        vt->esc_s = 4;
        return;
    case ')': /* ESC ) */
        vt->esc_s = 5;
        return;
    case '#': /* ESC # */
        vt->esc_s = 6;
        return;
    case 'P': /* ESC P (DCS, Device Control String) */
        vt->esc_s = 7;
        return;
    case ']': /* ESC ] (OSC, Operating System Command) */
        vt->esc_s = 8;
        return;
    case 'D': /* Cursor down */
    case 'M': /* Cursor up */
        x = vt->curx;
        if (c == 'D') { /* Down. */
            y = vt->cury + 1;
            if (y == vt->scroll_bottom + 1)
                wscroll(vt, S_UP);
            else if (vt->cury < vt->ys)
                locate_curs(vt, x, y);
        }
        if (c == 'M')  { /* Up. */
            y = vt->cury - 1;
            if (y == vt->scroll_top - 1)
                wscroll(vt, S_DOWN);
            else if (y >= 0)
                locate_curs(vt, x, y);
        }
        break;
    case 'E': /* CR + NL */
        wputs(vt, "\r\n");
        break;
    case '7': /* Save attributes and cursor position */
    case 's':
        vt->savex = vt->curx;
        vt->savey = vt->cury;
        vt->saveattr = vt->attr;
        vt->save_fg = vt->vt_fg;
        vt->save_bg = vt->vt_bg;
        vt->savecharset = vt->vt_charset;
        vt->savetrans[0] = vt->vt_trans[0];
        vt->savetrans[1] = vt->vt_trans[1];
        break;
    case '8': /* Restore them */
    case 'u':
        vt->vt_charset = vt->savecharset;
        vt->vt_trans[0] = vt->savetrans[0];
        vt->vt_trans[1] = vt->savetrans[1];
        wsetfgcol(vt, vt->save_fg);
        wsetbgcol(vt, vt->save_bg);
        wsetattr(vt, vt->saveattr);
        locate_curs(vt, vt->savex, vt->savey);
        break;
    case '=': /* Keypad into applications mode */
        vt->vt_keypad = APPL;
        break;
    case '>': /* Keypad into numeric mode */
        vt->vt_keypad = NORMAL;
        break;
    case 'Z': /* Report terminal type */
        v_termout(vt, "\033[?1;0c", 0);
        break;
    case 'c': /* Reset to initial state */
        f = XA_NORMAL;
        wsetattr(vt, f);
        vt->vt_wrap = 1;
        vt->vt_crlf = vt->vt_insert = 0;
        //   vt_init(vt_type, vt->vt_fg, vt->vt_bg, vt->vt_wrap, 0, 0);
        locate_curs(vt, 0, 0);
        break;
    case 'H': /* Set tab in current position */
        x = vt->curx;
        if (x > 159)
            x = 159;
        vt->vt_tabs[x / 32] |= 1 << (x % 32);
        break;
    case 'N': /* G2 character set for next character only*/
    case 'O': /* G3 "				"    */
    case '<': /* Exit vt52 mode */
    default:
        /* ALL IGNORED */
        break;
    }
    vt->esc_s = 0;
}

/* ESC [ ... [hl] seen. */
static void ansi_mode(struct vc_state *vt, int on_off)
{
  int i;

  for (i = 0; i <= vt->ptr; i++) {
    switch (vt->escparms[i]) {
      case 4: /* Insert mode  */
        vt->vt_insert = on_off;
        break;
      case 20: /* Return key mode */
        vt->vt_crlf = on_off;
        break;
    }
  }
}

/*
 * ESC [ ... was seen the last time. Process next character.
 */
static void state2(struct vc_state *vt, int c)
{
    short x, y, attr, f;
    char temp[32];

    /* See if a number follows */
    if (c >= '0' && c <= '9') {
        vt->escparms[vt->ptr] = 10*vt->escparms[vt->ptr] + c - '0';
        return;
    }
    /* Separation between numbers ? */
    if (c == ';') {
        if (vt->ptr < (int)ARRAY_SIZE(vt->escparms) - 1)
        vt->ptr++;
        return;
    }
    /* ESC [ ? sequence */
    if (vt->escparms[0] == 0 && vt->ptr == 0 && c == '?') {
        vt->esc_s = 3;
        return;
    }

    /* Process functions with zero, one, two or more arguments */
    switch (c) {
    case 'A':
    case 'B':
    case 'C':
    case 'D': /* Cursor motion */
        if ((f = vt->escparms[0]) == 0)
            f = 1;
        x = vt->curx;
        y = vt->cury;
        x += f * ((c == 'C') - (c == 'D'));
        if (x < 0)
            x = 0;
        if (x >= vt->xs)
            x = vt->xs - 1;
        if (c == 'B') { /* Down. */
            y += f;
            if (y >= vt->ys)
                y = vt->ys - 1;
            if (y >= vt->scroll_bottom + 1)
                y = vt->scroll_bottom;
        }
        if (c == 'A') { /* Up. */
            y -= f;
            if (y < 0)
            y = 0;
            if (y <= vt->scroll_top - 1)
            y = vt->scroll_top;
        }
        locate_curs(vt, x, y);
        break;
    case 'X': /* Character erasing (ECH) */
        if ((f = vt->escparms[0]) == 0)
            f = 1;
        wclrch(vt, f);
        break;
    case 'K': /* Line erasing */
        switch (vt->escparms[0]) {
        case 0:
            wclreol(vt);
            break;
        case 1:
            //mc_wclrbol(vt);
            break;
        case 2:
            wclrel(vt);
            break;
        }
        break;
    case 'J': /* Screen erasing */
        switch (vt->escparms[0]) {
        case 0:
            wclreos(vt);
            break;
        case 1:
            wclrbos(vt);
            break;
        case 2:
            winclr(vt);
            break;
        }
        break;
    case 'n': /* Requests / Reports */
        switch(vt->escparms[0]) {
        case 5: /* Status */
            v_termout(vt, "\033[0n", 0);
            break;
        case 6:	/* Cursor Position */
            sprintf(temp, "\033[%d;%dR", vt->cury + 1, vt->curx + 1);
            v_termout(vt, temp, 0);
            break;
        }
        break;
    case 'c': /* Identify Terminal Type */
        v_termout(vt, "\033[?1;2c", 0);
        break;
    case 'x': /* Request terminal parameters. */
        /* Always answers 19200-8N1 no options. */
        sprintf(temp, "\033[%c;1;1;120;120;1;0x", vt->escparms[0] == 1 ? '3' : '2');
        v_termout(vt, temp, 0);
        break;
    case 's': /* Save attributes and cursor position */
        vt->savex = vt->curx;
        vt->savey = vt->cury;
        vt->saveattr = vt->attr;
        vt->save_fg = vt->vt_fg;
        vt->save_bg = vt->vt_bg;
        vt->savecharset = vt->vt_charset;
        vt->savetrans[0] = vt->vt_trans[0];
        vt->savetrans[1] = vt->vt_trans[1];
        break;
    case 'u': /* Restore them */
        vt->vt_charset = vt->savecharset;
        vt->vt_trans[0] = vt->savetrans[0];
        vt->vt_trans[1] = vt->savetrans[1];
        wsetfgcol(vt, vt->save_fg);
        wsetbgcol(vt, vt->save_bg);
        wsetattr(vt, vt->saveattr);
        locate_curs(vt, vt->savex, vt->savey);
        break;
    case 'h':
        ansi_mode(vt, 1);
        break;
    case 'l':
        ansi_mode(vt, 0);
        break;
    case 'H':
    case 'f': /* Set cursor position */
        if ((y = vt->escparms[0]) == 0)
            y = 1;
        if ((x = vt->escparms[1]) == 0)
            x = 1;
        if (vt->vt_om)
            y += vt->scroll_top;
        locate_curs(vt, x - 1, y - 1);
        break;
    case 'G': /* HPA: Cursor to column x */
    case '`':
        if ((x = vt->escparms[1]) == 0)
            x = 1;
        locate_curs(vt, x - 1, vt->cury);
        break;
    case 'g': /* Clear tab stop(s) */
        if (vt->escparms[0] == 0) {
            x = vt->curx;
            if (x > 159)
            x = 159;
            vt->vt_tabs[x / 32] &= ~(1 << x % 32);
        }
        if (vt->escparms[0] == 3)
            for(x = 0; x < 5; x++)
            vt->vt_tabs[x] = 0;
        break;
    case 'm': /* Set attributes */
        attr = mc_wgetattr((vt));
        for (f = 0; f <= vt->ptr; f++) {
            if (vt->escparms[f] >= 30 && vt->escparms[f] <= 37)
                wsetfgcol(vt, vt->escparms[f] - 30);
            if (vt->escparms[f] >= 40 && vt->escparms[f] <= 47)
                wsetbgcol(vt, vt->escparms[f] - 40);
            switch (vt->escparms[f]) {
            case 0:
                attr = XA_NORMAL;
                wsetfgcol(vt, vt->vt_fg);
                wsetbgcol(vt, vt->vt_bg);
                break;
            case 1:
                attr |= XA_BOLD;
                break;
            case 4:
                attr |= XA_UNDERLINE;
                break;
            case 5:
                attr |= XA_BLINK;
                break;
            case 7:
                attr |= XA_REVERSE;
                break;
            case 22: /* Bold off */
                attr &= ~XA_BOLD;
                break;
            case 24: /* Not underlined */
                attr &=~XA_UNDERLINE;
                break;
            case 25: /* Not blinking */
                attr &= ~XA_BLINK;
                break;
            case 27: /* Not reverse */
                attr &= ~XA_REVERSE;
                break;
            case 39: /* Default fg color */
                wsetfgcol(vt, vt->vt_fg);
                break;
            case 49: /* Default bg color */
                wsetbgcol(vt, vt->vt_bg);
                break;
            }
        }
        wsetattr(vt, attr);
        break;
    case 'L': /* Insert lines */
        if ((x = vt->escparms[0]) == 0)
            x = 1;
        for (f = 0; f < x; f++)
            mc_winsline(vt);
        break;
    case 'M': /* Delete lines */
        if ((x = vt->escparms[0]) == 0)
            x = 1;
        for (f = 0; f < x; f++)
            mc_wdelline(vt);
        break;
    case 'P': /* Delete Characters */
        if ((x = vt->escparms[0]) == 0)
            x = 1;
        for (f = 0; f < x; f++)
            mc_wdelchar(vt);
        break;
    case '@': /* Insert Characters */
        if ((x = vt->escparms[0]) == 0)
            x = 1;
        for (f = 0; f < x; f++)
            winschar(vt);
        break;
    case 'r': /* Set scroll region */
        set_scrl_reg(vt, vt->escparms[0], vt->escparms[1]);
        locate_curs(vt, 0, vt->scroll_top);
        break;
    case 'i': /* Printing */
    case 'y': /* Self test modes */
    default:
        /* IGNORED */
        // printf("minicom-debug: Unknown ESC sequence '%c'\n", c);
        break;
    }
    /* Ok, our escape sequence is all done */
    vt->esc_s = 0;
    vt->ptr = 0;
    memset(vt->escparms, 0, sizeof(vt->escparms));
    return;
}

/* ESC [? ... [hl] seen. */
static void dec_mode(struct vc_state *vt, int on_off)
{
    int i;

    for (i = 0; i <= vt->ptr; i++) {
        switch (vt->escparms[i]) {
        case 1: /* Cursor keys in cursor/appl mode */
            vt->vt_cursor_mode = on_off ? APPL : NORMAL;
            break;
        case 6: /* Origin mode. */
            vt->vt_om = on_off;
            locate_curs(vt, 0, vt->scroll_top);
            break;
        case 7: /* Auto wrap */
            vt->vt_wrap = on_off;
            break;
        case 25: /* Cursor on/off */
            vt->vt_cursor_on = on_off ? CNORMAL : CNONE;
            break;
        case 67: /* Backspace key sends. (FIXME: vt420) */
            /* setbackspace(on_off ? 8 : 127); */
            break;
        case 47: /* Alternate screen */
        case 1047:
        case 1049:
    /*
            if ((us_alternate && on_off) || !on_off)
            {
                vt = us;

                mc_clear_window_simple(us_alternate);
                mc_wclose(us_alternate, 1);
                us_alternate = NULL;
            }

            if (on_off)
            {
                us_alternate = mc_wopen(0, 0, COLS - 1, us->y2, BNONE, XA_NORMAL,
                                        tfcolor, tbcolor,  1, 0, 0);
                vt = us_alternate;
            }

            {
                char b[10];

                snprintf(b, sizeof(b),
                        "\e[?%d%c", vt->escparms[i], on_off ? 'h' : 'l');
                b[sizeof(b) - 1] = 0;

                wputs(stdwin, b);
            }

            if (on_off)
            mc_clear_window_simple(us_alternate);

            break;
    */
        default: /* Mostly set up functions */
            /* IGNORED */
            break;
        }
    }
}

/*
 * ESC [ ? ... seen.
 */
static void state3(struct vc_state *vt, int c)
{
    /* See if a number follows */
    if (c >= '0' && c <= '9') {
        vt->escparms[vt->ptr] = 10*vt->escparms[vt->ptr] + c - '0';
        return;
    }
    switch (c) {
    case 'h':
        dec_mode(vt, 1);
        break;
    case 'l':
        dec_mode(vt, 0);
        break;
    case 'i': /* Printing */
    case 'n': /* Request printer status */
    default:
        /* IGNORED */
        break;
    }
    vt->esc_s = 0;
    vt->ptr = 0;
    memset(vt->escparms, 0, sizeof(vt->escparms));
    return;
}

/*
 * ESC ( Seen.
 */
static void state4(struct vc_state *vt, int c)
{
    /* Switch Character Sets. */
    switch (c) {
    case 'A':
    case 'B':
        vt->vt_trans[0] = vt_map[0];
        break;
    case '0':
    case 'O':
        vt->vt_trans[0] = vt_map[1];
        break;
    }
    vt->esc_s = 0;
}

/*
 * ESC ) Seen.
 */
static void state5(struct vc_state *vt, int c)
{
    /* Switch Character Sets. */
    switch (c) {
    case 'A':
    case 'B':
        vt->vt_trans[1] = vt_map[0];
        break;
    case 'O':
    case '0':
        vt->vt_trans[1] = vt_map[1];
        break;
    }
    vt->esc_s = 0;
}

/*
 * ESC # Seen.
 */
static void state6(struct vc_state *vt, int c)
{
    int x, y;

    /* Double height, double width and selftests. */
    switch (c) {
    case '8':
        /* Selftest: fill screen with E's */
        vt->vt_doscroll = 0;
        locate_curs(vt, 0, 0);
        for (y = 0; y < vt->ys; y++) {
            locate_curs(vt, 0, y);
            for (x = 0; x < vt->xs; x++)
                wputc(vt, 'E');
        }
        locate_curs(vt, 0, 0);
        vt->vt_doscroll = 1;
        wredraw(vt, 1);
        break;
    default:
        /* IGNORED */
        break;
    }
    vt->esc_s = 0;
}

/*
 * ESC P Seen.
 */
static void state7(struct vc_state *vt, int c)
{
    /*
    * Device dependent control strings. The Minix virtual console package
    * uses these sequences. We can only turn cursor on or off, because
    * that's the only one supported in termcap. The rest is ignored.
    */
    static char buf[17];
    static int pos = 0;
    static int state = 0;

    if (c == ESC) {
        state = 1;
        return;
    }
    if (state == 1) {
        buf[pos] = 0;
        pos = 0;
        state = 0;
        vt->esc_s = 0;
        if (c != '\\')
            return;
        /* Process string here! */
        if (!strcmp(buf, "cursor.on"))
            vt->vt_cursor_on = 1;
        if (!strcmp(buf, "cursor.off"))
            vt->vt_cursor_on = 0;
        if (!strcmp(buf, "linewrap.on"))
            vt->vt_wrap = 1;
        if (!strcmp(buf, "linewrap.off"))
            vt->vt_wrap = 0;
        return;
    }
    if (pos > 15)
        return;
    buf[pos++] = c;
}

/*
 * ESC ] Seen.
 */
static void state8(struct vc_state *vt, int c)
{
  /*
   * Operating system commands.
   * No support is currently implemented, they are simply thrown away.
   * The sequences end with '\a' (BEL, terminal bell) or ESC-\ (ST).
   */
    static int state = 0;
    switch (c) {
    case 7:
        /* Got BEL - done */
        state = 0;
        vt->esc_s = 0;
        return;
    case ESC:
        /* Possibly start of ST */
        state = 1;
        return;
    case '\\':
        /* Possibly end of ST */
        if (state == 1) {
            state = 0;
            vt->esc_s = 0;
            return;
        }
        break;
    };
    state = 0;
}

static void output_s(struct vc_state *vt, const char *s)
{
    wputs(vt, s);
}

static void output_c(struct vc_state *vt, const char c)
{
    wputc(vt, c);
}

static void vt_out(struct vc_state *vt, int ch, wchar_t wc)
{
    int f;
    unsigned char c;
    int go_on = 0;

    if (!ch)
        return;

    c = (unsigned char)ch;

    /* Process <31 chars first, even in an escape sequence. */
    switch (c) {
    case 5: /* AnswerBack for vt100's */
        // v_termout(P_ANSWERBACK, 0);
        break;
    case '\r': /* Carriage return */
        cr(vt);
        if (vt->vt_addlf)
            lf(vt);
        break;
    case '\t': /* Non - destructive TAB */
        /* Find next tab stop. */
        for (f = vt->curx + 1; f < 160; f++)
            if (vt->vt_tabs[f / 32] & (1u << f % 32))
            break;
        if (f >= vt->xs)
            f = vt->xs - 1;
        locate_curs(vt, f, vt->cury);
        break;
    case 013: /* Old Minix: CTRL-K = up */
        locate_curs(vt, vt->curx, vt->cury - 1);
        break;
    case '\f': /* Form feed: clear screen. */
        winclr(vt);
        locate_curs(vt, 0, 0);
        break;
    case 14:
        vt->vt_charset = 1;
        break;
    case 15:
        vt->vt_charset = 0;
        break;
    case 24:
    case 26:  /* Cancel escape sequence. */
        vt->esc_s = 0;
        break;
    case ESC: /* Begin escape sequence */
        vt->esc_s = 1;
        break;
    case 128+ESC: /* Begin ESC [ sequence. */
        vt->esc_s = 2;
        break;
    case '\n':
        if(vt->vt_addcr)
            cr(vt);
        lf(vt);
        break;
    case '\b':
        bs(vt); /* Backspace */
        break;
    case 7: /* Bell */
        if (vt->esc_s == 8)
            go_on = 1;
        else
            output_c(vt, c);
        break;
    default:
        go_on = 1;
        break;
    }
    if (!go_on)
        return;

    /* Now see which state we are in. */
    switch (vt->esc_s) {
    case 0:
        if (vt->vt_insert)
            winsnchar(vt, c, 1);
        else
            wputc(vt, c);
        break;
    case 1: /* ESC seen */
        state1(vt, c);
        break;
    case 2: /* ESC [ ... seen */
        state2(vt, c);
        break;
    case 3:
        state3(vt, c);
        break;
    case 4:
        state4(vt, c);
        break;
    case 5:
        state5(vt, c);
        break;
    case 6:
        state6(vt, c);
        break;
    case 7:
        state7(vt, c);
        break;
    case 8:
        state8(vt, c);
        break;
    }
}

/* Translate keycode to escape sequence. */
// void vt_send(int c)
// {
//   char s[3];
//   int f;
//   int len = 1;

//   /* Special key? */
//   if (c < 256) {
//     /* Translate backspace key? */
//     if (c == K_ERA)
//       c = vt_bs;
//     s[0] = vt_outmap[c];  /* conversion 04.09.97 / jl */
//     s[1] = 0;
//     /* CR/LF mode? */
//     if (c == '\r' && vt->vt_crlf) {
//       s[1] = '\n';
//       s[2] = 0;
//       len = 2;
//     }
//     v_termout(s, len);
//     if (vt_nl_delay > 0 && c == '\r')
//       usleep(1000 * vt_nl_delay);
//     return;
//   }

//   /* Look up code in translation table. */
//   for (f = 0; vt_keys[f].code; f++)
//     if (vt_keys[f].code == c)
//       break;
//   if (vt_keys[f].code == 0)
//     return;

//   /* Now send appropriate escape code. */
//   v_termout("\033", 0);
//   if (vt_type == VT100) {
//     if (vt->vt_cursor_mode == NORMAL)
//       v_termout(vt_keys[f].vt100_st, 0);
//     else
//       v_termout(vt_keys[f].vt100_app, 0);
//   } else
//     v_termout(vt_keys[f].ansi, 0);
// }


/* Output a string to the modem. */
static void v_termout(struct vc_state *vt, const char *s, int len)
{
    const char *p;
    clear_cursor(vt, vt->curx, vt->cury);
    for (p = s; *p && p < s+len; p++)
        vt_out(vt, *p, 0);
    display_cursor(vt, vt->curx, vt->cury);
}

ssize_t vt_write(struct tty *tty, const u8 *buf, size_t count)
{
    v_termout(tty->driver_data, (const char*)buf, count);
    return MIN(strlen(buf), count);
}

void vt_close(struct tty *tty, struct file *f)
{
}
