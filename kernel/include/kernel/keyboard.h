// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef KERNEL_KEYBOARD_H
#define KERNEL_KEYBOARD_H

#include <kernel/types.h>

u8 keyboard_read(void);
void keyboard_init(void);
void keyboard_interrupt(void);

#endif
