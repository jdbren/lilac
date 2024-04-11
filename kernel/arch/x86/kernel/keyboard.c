// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/keyboard.h>
#include <lilac/tty.h>
#include <utility/keymap.h>
#include "idt.h"
#include "io.h"
#include "pic.h"
#include "apic.h"

#define KEYBOARD_DATA_PORT 0x60

extern void keyboard_handler(void);

inline u8 keyboard_read(void)
{
    return inb(KEYBOARD_DATA_PORT);
}

void keyboard_init(void)
{
    idt_entry(0x20 + 1, (u32)keyboard_handler, 0x08, INT_GATE);
    ioapic_entry(1, 0x20 + 1, 0, 0);
    printf("Keyboard initialized\n");
}

void keyboard_interrupt(void)
{
    s8 keycode;

    keycode = inb(KEYBOARD_DATA_PORT);

    if (keycode >= 0 && keyboard_map[keycode])
        terminal_putchar(keyboard_map[keycode]);

    /* Send End of Interrupt (EOI) to master PIC */
    apic_eoi();
}
