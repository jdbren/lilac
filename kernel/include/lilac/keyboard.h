// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef KERNEL_KEYBOARD_H
#define KERNEL_KEYBOARD_H

#include <lilac/types.h>

#define KB_SHIFT    1
#define KB_CTRL     2
#define KB_ALT      4
#define KB_CAPSLOCK 8

// #define FN(code) (code >= 59 && code <= 68 ? code % 58 : 0)

struct kbd_event {
    u8 keycode;
    u8 status;
};


u8 keyboard_read(void);
void keyboard_init(void);
void set_console(int value);

#endif
