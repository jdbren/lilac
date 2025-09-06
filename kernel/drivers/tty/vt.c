/*
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
#define mc_wsetattr(w,a) ( (w)->attr = (a) )


int vt_open(struct tty *tty, struct file *file);
void vt_close(struct tty *tty, struct file *f);
ssize_t vt_write(struct tty *tty, const u8 *buf, size_t count);

const struct tty_operations vt_tty_ops = {
    .open = vt_open,
    .close = vt_close,
    .write = vt_write,
};

static struct vt_state vt_cons[8] = {
    [0 ... 7] = {
        // .con_ops = ,
        .vt_echo = 1,
        .vt_wrap = 1,
        .vt_addlf = 0,
        .vt_addcr = 0,
        .vt_keypad = 0,
        .vt_cursor = 1,
        .vt_asis = 0,
        .vt_insert = 0,
        .vt_crlf = 0,
        .vt_om = 0,
        .vt_doscroll = 1,
        .newy1 = 0,
        .newy2 = 23,
        .savecol = 112,
        .xs = 80,
        .ys = 24,
    }
};

void vt_out(struct vt_state *vt, int ch, wchar_t wc);

int vt_open(struct tty *tty, struct file *file)
{
    fbcon_open(tty, file);
    if (!tty->driver_data) {
        tty->driver_data = &vt_cons[tty->index];
    }
    console_clear(&consoles[0]);
    return 0;
}


// ------------------

void vt_scroll(struct vt_state *v) {
    // console_scroll_region(v->scroll_top, v->newy2);
    console_newline(&consoles[0]);
    v->cury = v->newy2;
}

/*
 * Set cursor type.
 */
void mc_wcursor(struct vt_state *vt, int type)
{
  vt->vt_cursor = type;
//   if (win->direct) {
//     _cursor(type);
//     if (dirflush)
//       mc_wflush();
//   }
}

void mc_wredraw(struct vt_state *vt, int i)
{

}

// Move cursor to (x,y)
void mc_wlocate(struct vt_state *v, int x, int y) {
    if (x < 0) x = 0;
    if (x >= v->xs) x = v->xs - 1;
    if (y < 0) y = 0;
    if (y >= v->ys) y = v->ys - 1;
    v->curx = x;
    v->cury = y;
    // console_set_cursor(x, y); // your console driver
}

// Set scroll region (DECSTBM)
void mc_wsetregion(struct vt_state *v, int top, int bottom) {
    if (top < 0) top = 0;
    if (bottom >= v->ys) bottom = v->ys - 1;
    if (top >= bottom) { // invalid → whole screen
        top = 0; bottom = v->ys - 1;
    }
    v->newy1 = top;
    v->newy2 = bottom;
}

// Write one character at cursor
// void mc_wputc(struct vt_state *v, char c) {
//     console_putchar_at(&consoles[0], c, v->curx, v->cury);

//     v->curx++;
//     if (v->curx >= v->xs) {
//         if (v->vt_wrap) {
//             v->curx = 0;
//             v->cury++;
//         } else {
//             v->curx = v->xs - 1;
//         }
//     }

//     if (v->cury > v->newy2) {
//         if (v->vt_doscroll)
//             vt_scroll(v);
//         else
//             v->cury = v->newy2;
//     }
// }

void mc_wscroll(struct vt_state *v, int dir) {
    if (dir == S_UP) {
        console_newline(&consoles[0]);
        // if (v->cury > v->newy2)
        //     v->cury = v->newy2;
    } else if (dir == S_DOWN) {
        // console_scroll_region_down(v->scroll_top, v->newy2);
        // if (v->cury < v->scroll_top)
        //     v->cury = v->scroll_top;
    }
}

/*
 * Print a character in a window.
 */
void mc_wputc(struct vt_state *vt, wchar_t c)
{
  int mv = 0;

  switch(c) {
    case '\r':
      vt->curx = 0;
      mv++;
      break;
    case '\b':
      if (vt->curx == 0)
        break;
      vt->curx--;
      mv++;
      break;
    case '\007':
      // mc_wbell();
      break;
    case '\t':
      do {
        mc_wputc(vt, ' '); /* Recursion! */
      } while (vt->curx % 8);
      break;
    case '\n':
      // if (vt->autocr)
        vt->curx = 0;
      /* FALLTHRU */
    default:
      /* See if we need to scroll/move. (vt100 behaviour!) */
      if (c == '\n' || (vt->curx >= vt->xs && vt->vt_wrap)) {
        if (c != '\n')
          vt->curx = 0;
        vt->cury++;
        mv++;
        if (vt->cury == vt->ys - 1) {
          if (vt->vt_doscroll)
            mc_wscroll(vt, S_UP);
          else
            vt->cury = vt->ys;
        }
        if (vt->cury >= vt->ys)
          vt->cury = vt->ys - 1;
      }
      /* Now write the character. */
      if (c != '\n') {
	if (!vt->vt_wrap && vt->curx >= vt->xs)
	  c = '>';
        console_putchar_at(&consoles[0], c, vt->curx,
               vt->cury);
        if (++vt->curx >= vt->xs && !vt->vt_wrap) {
          // vt->curx--;
          vt->curx = 0; /* Force to move */
          mv++;
        }
      }
      break;
  }
  // if (mv && vt->vt_direct)
  //   _gotoxy(vt->curx, vt->cury);

  // if (vt->direct && dirflush && !_intern)
  //   mc_wflush();
}

// Insert mode version (shift chars right, then insert)
void mc_winschar2(struct vt_state *v, char c, int count) {
    // console_insert_char_at(v->curx, v->cury, c, v->attr, v->color, count);
    // Usually console driver handles shifting + drawing
}

// Clear entire window
void mc_wclear(struct vt_state *v) {
    console_clear(&consoles[0]);
    v->curx = 0;
    v->cury = 0;
}

void mc_wsetfgcol(struct vt_state *vt, int col)
{
    // graphics_setcolor()
    vt->vt_fg = col;
}

void mc_wsetbgcol(struct vt_state *vt, int col)
{
    // graphics_setcolor()
    vt->vt_bg = col;
}

// Clear a single line completely
void mc_wclrch(struct vt_state *vt, int y)
{
    for (int x = 0; x < vt->xs; x++) {
        console_putchar_at(&consoles[0], ' ', x, y);
    }
}

// Clear from cursor → end of line
void mc_wclreol(struct vt_state *vt)
{
    for (int x = vt->curx; x < vt->xs; x++) {
        console_putchar_at(&consoles[0], ' ', x, vt->cury);
    }
}

// Clear from beginning of line → cursor
void mc_wclrel(struct vt_state *vt)
{
    for (int x = 0; x <= vt->curx; x++) {
        console_putchar_at(&consoles[0], ' ', x, vt->cury);
    }
}

// Clear from cursor → end of screen
void mc_wclreos(struct vt_state *vt)
{
    // current line
    mc_wclreol(vt);

    // all lines below
    for (int y = vt->cury + 1; y < vt->ys; y++) {
        mc_wclrch(vt, y);
    }
}

// Clear from beginning of screen → cursor
void mc_wclrbos(struct vt_state *vt)
{
    // all lines above
    for (int y = 0; y < vt->cury; y++) {
        mc_wclrch(vt, y);
    }

    // current line up to cursor
    mc_wclrel(vt);
}

// Clear entire screen
void mc_winclr(struct vt_state *vt)
{
    for (int y = 0; y < vt->ys; y++) {
        mc_wclrch(vt, y);
    }
}

// Insert a blank line at cury, scroll down inside scroll region
void mc_winsline(struct vt_state *vt)
{
    // if (vt->cury < vt->newy2) {
    //     for (int y = vt->newy2; y > vt->cury; y--) {
    //         for (int x = 0; x < vt->width; x++) {
    //             char c = vt->data[(y - 1) * vt->width + x];
    //             vt->data[y * vt->width + x] = c;
    //             console_putchar_at(c, x, y);
    //         }
    //     }
    //     // clear the cursor line
    //     for (int x = 0; x < vt->width; x++) {
    //         vt->data[vt->cury * vt->width + x] = ' ';
    //         console_putchar_at(' ', x, vt->cury);
    //     }
    // }
}

// Delete current line, scroll up inside scroll region
void mc_wdelline(struct vt_state *vt)
{
    // if (vt->cury < vt->newy2) {
    //     for (int y = vt->cury; y < vt->newy2; y++) {
    //         for (int x = 0; x < vt->width; x++) {
    //             char c = vt->data[(y + 1) * vt->width + x];
    //             vt->data[y * vt->width + x] = c;
    //             console_putchar_at(c, x, y);
    //         }
    //     }
    //     // clear bottom line
    //     for (int x = 0; x < vt->width; x++) {
    //         vt->data[vt->newy2 * vt->width + x] = ' ';
    //         console_putchar_at(' ', x, vt->newy2);
    //     }
    // }
}

// Delete char at cursor, shift rest of line left
void mc_wdelchar(struct vt_state *vt)
{
    int y = vt->cury;
    int x = vt->curx;
    // for (int i = x; i < vt->width - 1; i++) {
    //     char c = vt->data[y * vt->width + (i + 1)];
    //     vt->data[y * vt->width + i] = c;
    //     console_putchar_at(c, i, y);
    // }
    // // clear last cell in line
    // vt->data[y * vt->width + (vt->width - 1)] = ' ';
    // console_putchar_at(' ', vt->width - 1, y);
}

// Insert blank char at cursor, shift rest of line right
void mc_winschar(struct vt_state *vt)
{
    // int y = vt->cury;
    // int x = vt->curx;
    // for (int i = vt->width - 1; i > x; i--) {
    //     char c = vt->data[y * vt->width + (i - 1)];
    //     vt->data[y * vt->width + i] = c;
    //     console_putchar_at(c, i, y);
    // }
    // // place blank at cursor
    // vt->data[y * vt->width + x] = ' ';
    // console_putchar_at(' ', x, y);
}


#define S_UP   1
#define S_DOWN 2

// Write a null-terminated string to the VT
void mc_wputs(struct vt_state *v, const char *s) {
    while (*s) {
        mc_wputc(v, *s++);
    }
}


// --------------


/* Output a string to the modem. */
static void v_termout(struct vt_state *vt, const char *s, int len)
{
  const char *p;

  if (vt->vt_echo) {
    for (p = s; *p && p < s+len; p++) {
      vt_out(vt, *p, 0);
      if (!vt->vt_addlf && *p == '\r')
        vt_out(vt, '\n', 0);
    }
    // mc_wflush();
  }

//   (*termout)(s, len);
}

/*
 * Escape code handling.
 */

/*
 * ESC was seen the last time. Process the next character.
 */
static void state1(struct vt_state *vt, int c)
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
        if (y == vt->newy2 + 1)
          mc_wscroll(vt, S_UP);
        else if (vt->cury < vt->ys)
          mc_wlocate(vt, x, y);
      }
      if (c == 'M')  { /* Up. */
        y = vt->cury - 1;
        if (y == vt->newy1 - 1)
          mc_wscroll(vt, S_DOWN);
        else if (y >= 0)
          mc_wlocate(vt, x, y);
      }
      break;
    case 'E': /* CR + NL */
      mc_wputs(vt, "\r\n");
      break;
    case '7': /* Save attributes and cursor position */
    case 's':
      vt->savex = vt->curx;
      vt->savey = vt->cury;
      vt->saveattr = vt->attr;
      vt->savecol = vt->color;
      vt->savecharset = vt->vt_charset;
      vt->savetrans[0] = vt->vt_trans[0];
      vt->savetrans[1] = vt->vt_trans[1];
      break;
    case '8': /* Restore them */
    case 'u':
      vt->vt_charset = vt->savecharset;
      vt->vt_trans[0] = vt->savetrans[0];
      vt->vt_trans[1] = vt->savetrans[1];
      vt->color = vt->savecol; /* HACK should use mc_wsetfgcol etc */
      mc_wsetattr(vt, vt->saveattr);
      mc_wlocate(vt, vt->savex, vt->savey);
      break;
    case '=': /* Keypad into applications mode */
      vt->vt_keypad = APPL;
    //   if (vt_keyb)
    //     (*vt_keyb)(vt->vt_keypad, vt->vt_cursor);
      break;
    case '>': /* Keypad into numeric mode */
      vt->vt_keypad = NORMAL;
    //   if (vt_keyb)
    //     (*vt_keyb)(vt->vt_keypad, vt->vt_cursor);
      break;
    case 'Z': /* Report terminal type */
    //   if (vt_type == VT100)
        v_termout(vt, "\033[?1;0c", 0);
    //   else
    //     v_termout("\033[?c", 0);
      break;
    case 'c': /* Reset to initial state */
      f = XA_NORMAL;
      mc_wsetattr(vt, f);
    //   vt->wrap = (vt_type != VT100);
    //   if (vt_wrap != -1)
    // vt->wrap = vt_wrap;
      vt->vt_wrap = 1;
      vt->vt_crlf = vt->vt_insert = 0;
    //   vt_init(vt_type, vt->vt_fg, vt->vt_bg, vt->vt_wrap, 0, 0);
      mc_wlocate(vt, 0, 0);
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
static void ansi_mode(struct vt_state *vt, int on_off)
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
static void state2(struct vt_state *vt, int c)
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
  if (vt->escparms[0] == 0 && vt->ptr == 0 && c == '?')
    {
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
        if (y >= vt->newy2 + 1)
          y = vt->newy2;
      }
      if (c == 'A') { /* Up. */
        y -= f;
        if (y < 0)
          y = 0;
        if (y <= vt->newy1 - 1)
          y = vt->newy1;
      }
      mc_wlocate(vt, x, y);
      break;
    case 'X': /* Character erasing (ECH) */
      if ((f = vt->escparms[0]) == 0)
        f = 1;
      mc_wclrch(vt, f);
      break;
    case 'K': /* Line erasing */
      switch (vt->escparms[0]) {
        case 0:
          mc_wclreol(vt);
          break;
        case 1:
        //   mc_wclrbol(vt);
          break;
        case 2:
          mc_wclrel(vt);
          break;
      }
      break;
    case 'J': /* Screen erasing */
      x = vt->color;
      y = vt->attr;
    //   if (vt_type == ANSI) {
        mc_wsetattr(vt, XA_NORMAL);
        mc_wsetfgcol(vt, WHITE);
        mc_wsetbgcol(vt, BLACK);
    //   }
      switch (vt->escparms[0]) {
        case 0:
          mc_wclreos(vt);
          break;
        case 1:
          mc_wclrbos(vt);
          break;
        case 2:
          mc_winclr(vt);
          break;
      }
    //   if (vt_type == ANSI) {
        vt->color = x;
        vt->attr = y;
    //   }
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
    //   if (vt_type == VT100) {
        v_termout(vt, "\033[?1;2c", 0);
        break;
    //   }
    //   v_termout("\033[?c", 0);
    //   break;
    case 'x': /* Request terminal parameters. */
      /* Always answers 19200-8N1 no options. */
      sprintf(temp, "\033[%c;1;1;120;120;1;0x", vt->escparms[0] == 1 ? '3' : '2');
      v_termout(vt, temp, 0);
      break;
    case 's': /* Save attributes and cursor position */
      vt->savex = vt->curx;
      vt->savey = vt->cury;
      vt->saveattr = vt->attr;
      vt->savecol = vt->color;
      vt->savecharset = vt->vt_charset;
      vt->savetrans[0] = vt->vt_trans[0];
      vt->savetrans[1] = vt->vt_trans[1];
      break;
    case 'u': /* Restore them */
      vt->vt_charset = vt->savecharset;
      vt->vt_trans[0] = vt->savetrans[0];
      vt->vt_trans[1] = vt->savetrans[1];
      vt->color = vt->savecol; /* HACK should use mc_wsetfgcol etc */
      mc_wsetattr(vt, vt->saveattr);
      mc_wlocate(vt, vt->savex, vt->savey);
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
        y += vt->newy1;
      mc_wlocate(vt, x - 1, y - 1);
      break;
    case 'G': /* HPA: Cursor to column x */
    case '`':
      if ((x = vt->escparms[1]) == 0)
        x = 1;
      mc_wlocate(vt, x - 1, vt->cury);
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
          mc_wsetfgcol(vt, vt->escparms[f] - 30);
        if (vt->escparms[f] >= 40 && vt->escparms[f] <= 47)
          mc_wsetbgcol(vt, vt->escparms[f] - 40);
        switch (vt->escparms[f]) {
          case 0:
            attr = XA_NORMAL;
            mc_wsetfgcol(vt, vt->vt_fg);
            mc_wsetbgcol(vt, vt->vt_bg);
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
            mc_wsetfgcol(vt, vt->vt_fg);
            break;
          case 49: /* Default bg color */
            mc_wsetbgcol(vt, vt->vt_bg);
            break;
        }
      }
      mc_wsetattr(vt, attr);
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
        mc_winschar(vt);
      break;
    case 'r': /* Set scroll region */
      if ((vt->newy1 = vt->escparms[0]) == 0)
        vt->newy1 = 1;
      if ((vt->newy2 = vt->escparms[1]) == 0)
        vt->newy2 = vt->ys;
      vt->newy1-- ; vt->newy2--;
      if (vt->newy1 < 0)
        vt->newy1 = 0;
      if (vt->newy2 < 0)
        vt->newy2 = 0;
      if (vt->newy1 >= vt->ys)
        vt->newy1 = vt->ys - 1;
      if (vt->newy2 >= vt->ys)
        vt->newy2 = vt->ys - 1;
      if (vt->newy1 >= vt->newy2) {
        vt->newy1 = 0;
        vt->newy2 = vt->ys - 1;
      }
      mc_wsetregion(vt, vt->newy1, vt->newy2);
      mc_wlocate(vt, 0, vt->newy1);
      break;
    case 'i': /* Printing */
    case 'y': /* Self test modes */
    default:
      /* IGNORED */
      if (0)
        {
          printf("minicom-debug: Unknown ESC sequence '%c'\n", c);
        }
      break;
  }
  /* Ok, our escape sequence is all done */
  vt->esc_s = 0;
  vt->ptr = 0;
  memset(vt->escparms, 0, sizeof(vt->escparms));
  return;
}

/* ESC [? ... [hl] seen. */
static void dec_mode(struct vt_state *vt, int on_off)
{
  int i;

  for (i = 0; i <= vt->ptr; i++) {
    switch (vt->escparms[i]) {
      case 1: /* Cursor keys in cursor/appl mode */
        vt->vt_cursor = on_off ? APPL : NORMAL;
        // if (vt_keyb)
        //   (*vt_keyb)(vt->vt_keypad, vt->vt_cursor);
        break;
      case 6: /* Origin mode. */
        vt->vt_om = on_off;
        mc_wlocate(vt, 0, vt->newy1);
        break;
      case 7: /* Auto wrap */
        vt->vt_wrap = on_off;
        break;
      case 25: /* Cursor on/off */
        mc_wcursor(vt, on_off ? CNORMAL : CNONE);
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

            mc_wputs(stdwin, b);
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
static void state3(struct vt_state *vt, int c)
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
static void state4(struct vt_state *vt, int c)
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
static void state5(struct vt_state *vt, int c)
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
static void state6(struct vt_state *vt, int c)
{
  int x, y;

  /* Double height, double width and selftests. */
  switch (c) {
    case '8':
      /* Selftest: fill screen with E's */
      vt->vt_doscroll = 0;
      vt->vt_direct = 0;
      mc_wlocate(vt, 0, 0);
      for (y = 0; y < vt->ys; y++) {
        mc_wlocate(vt, 0, y);
        for (x = 0; x < vt->xs; x++)
          mc_wputc(vt, 'E');
      }
      mc_wlocate(vt, 0, 0);
      vt->vt_doscroll = 1;
      mc_wredraw(vt, 1);
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
static void state7(struct vt_state *vt, int c)
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
      mc_wcursor(vt, CNORMAL);
    if (!strcmp(buf, "cursor.off"))
      mc_wcursor(vt, CNONE);
    if (!strcmp(buf, "linewrap.on")) {
    //   vt_wrap = -1;
      vt->vt_wrap = 1;
    }
    if (!strcmp(buf, "linewrap.off")) {
    //   vt_wrap = -1;
      vt->vt_wrap = 0;
    }
    return;
  }
  if (pos > 15)
    return;
  buf[pos++] = c;
}

/*
 * ESC ] Seen.
 */
static void state8(struct vt_state *vt, int c)
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

static void output_s(struct vt_state *vt, const char *s)
{
  mc_wputs(vt, s);
}

static void output_c(struct vt_state *vt, const char c)
{
  mc_wputc(vt, c);
}

void vt_out(struct vt_state *vt, int ch, wchar_t wc)
{
  static unsigned char last_ch;
  int f;
  unsigned char c;
  int go_on = 0;

  if (!ch)
    return;

  c = (unsigned char)ch;
  last_ch = c;

  /* Process <31 chars first, even in an escape sequence. */
  switch (c) {
    case 5: /* AnswerBack for vt100's */
    //   if (vt_type != VT100) {
    //     go_on = 1;
    //     break;
    //   }
    //   v_termout(P_ANSWERBACK, 0);
      break;
    case '\r': /* Carriage return */
      mc_wputc(vt, c);
      if (vt->vt_addlf)
        output_c(vt, '\n');
      break;
    case '\t': /* Non - destructive TAB */
      /* Find next tab stop. */
      for (f = vt->curx + 1; f < 160; f++)
        if (vt->vt_tabs[f / 32] & (1u << f % 32))
          break;
      if (f >= vt->xs)
        f = vt->xs - 1;
      mc_wlocate(vt, f, vt->cury);
      break;
    case 013: /* Old Minix: CTRL-K = up */
      mc_wlocate(vt, vt->curx, vt->cury - 1);
      break;
    case '\f': /* Form feed: clear screen. */
      mc_winclr(vt);
      mc_wlocate(vt, 0, 0);
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
        mc_wputc(vt, '\r');
      output_c(vt, c);
      break;
    case '\b':
      output_c(vt, c); /* Backspace */
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
    case 0: /* Normal character */
    //   if (!using_iconv()) {
    //     c = vt_inmap[c];    /* conversion 04.09.97 / jl */
    //     if (vt_type == VT100 && vt->vt_trans[vt->vt_charset] && vt_asis == 0)
    //       c = vt->vt_trans[vt->vt_charset][c];
    //   }
    //   if (wc == 0)
    //     one_mbtowc (&wc, (char *)&c, 1); /* returns 1 */
    //   if (vt->vt_insert)
    //     mc_winschar2(vt, wc, 1);
    //   else
    //     mc_wputc(vt, wc);
      if (vt->vt_insert)
        mc_winschar2(vt, c, 1);
      else
        mc_wputc(vt, c);
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
//     if (vt->vt_cursor == NORMAL)
//       v_termout(vt_keys[f].vt100_st, 0);
//     else
//       v_termout(vt_keys[f].vt100_app, 0);
//   } else
//     v_termout(vt_keys[f].ansi, 0);
// }

ssize_t vt_write(struct tty *tty, const u8 *buf, size_t count)
{
    v_termout(tty->driver_data, (const char*)buf, count);
    return count;
}

void vt_close(struct tty *tty, struct file *f)
{
}
