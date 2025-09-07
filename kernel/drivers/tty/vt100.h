/*
 * vt100.h      Header file for the vt100 emulator.
 *
 *      $Id: vt100.h,v 1.4 2007-10-10 20:18:20 al-guest Exp $
 *
 *      This file is part of the minicom communications package,
 *      Copyright 1991-1995 Miquel van Smoorenburg.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef __MINICOM__SRC__VT100_H__
#define __MINICOM__SRC__VT100_H__

#include <stdint.h>

/* Keypad and cursor key modes. */
#define NORMAL      1
#define APPL        2

/* We want the ANSI offsetof macro to do some dirty stuff. */
#ifndef offsetof
#  define offsetof(type, member) ((int) &((type *)0)->member)
#endif

/* Values for the "flags". */
#define FL_ECHO     0x01    /* Local echo on/off. */
#define FL_DEL      0x02    /* Backspace or DEL */
#define FL_WRAP     0x04    /* Use autowrap. */
#define FL_ANSI     0x08    /* Type of term emulation */
#define FL_TAG      0x80    /* This entry is tagged. */
#define FL_SAVE     0x0f    /* Which portions of flags to save. */

/*
 * Possible attributes.
 */
#define XA_NORMAL       0
#define XA_BLINK        1
#define XA_BOLD         2
#define XA_REVERSE      4
#define XA_STANDOUT     8
#define XA_UNDERLINE    16
#define XA_ALTCHARSET   32
#define XA_BLANK        64

/*
 * Possible colors
 */
#define BLACK       0
#define RED         1
#define GREEN       2
#define YELLOW      3
#define BLUE        4
#define MAGENTA     5
#define CYAN        6
#define WHITE       7

#define COLATTR(fg, bg) (((fg) << 4) + (bg))
#define COLFG(ca)       ((ca) >> 4)
#define COLBG(ca)       ((ca) % 16)

/*
 * Possible borders.
 */
#define BNONE       0
#define BSINGLE     1
#define BDOUBLE     2

/*
 * Scrolling directions.
 */
#define S_UP        1
#define S_DOWN      2

/*
 * Cursor types.
 */
#define CNONE       0
#define CNORMAL     1

/*
 * Title Positions
 */
#define TLEFT       0
#define TMID        1
#define TRIGHT      2

/*
 * Allright, now the macro's for our keyboard routines.
 */

#define K_BS        8
#define K_ESC       27
#define K_STOP      256
#define K_F1        257
#define K_F2        258
#define K_F3        259
#define K_F4        260
#define K_F5        261
#define K_F6        262
#define K_F7        263
#define K_F8        264
#define K_F9        265
#define K_F10       266
#define K_F11       277
#define K_F12       278

#define K_HOME      267
#define K_PGUP      268
#define K_UP        269
#define K_LT        270
#define K_RT        271
#define K_DN        272
#define K_END       273
#define K_PGDN      274
#define K_INS       275
#define K_DEL       276

#define NUM_KEYS    23
#define KEY_OFFS    256

/* Here's where the meta keycode start. (512 + keycode). */
#define K_META      512

#define K_ERA       '\b'
#define K_KILL      ((int) -2)

extern const struct tty_operations vt_tty_ops;

#endif /* ! __MINICOM__SRC__VT100_H__ */
