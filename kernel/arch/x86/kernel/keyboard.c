// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/keyboard.h>

#include <lilac/tty.h>
#include <lilac/log.h>
#include <lilac/console.h>
#include <utility/keymap.h>
#include "idt.h"
#include "io.h"
#include "pic.h"
#include "apic.h"

#define KEYBOARD_DATA_PORT 0x60

static int console = 0;

inline u8 keyboard_read(void)
{
    return inb(KEYBOARD_DATA_PORT);
}

inline void set_console(int value)
{
    console = value;
}

__attribute__((interrupt))
void keyboard_int(struct interrupt_frame *frame)
{
    s8 keycode;

    keycode = inb(KEYBOARD_DATA_PORT);

    /* Send End of Interrupt (EOI) */
    apic_eoi();

    if (keycode >= 0 && keyboard_map[keycode]) {
        if (console)
            console_intr(keyboard_map[keycode]);
        else
            graphics_putchar(keyboard_map[keycode]);
    }
}

void keyboard_init(void)
{
    idt_entry(0x20 + 1, (u32)keyboard_int, 0x08, INT_GATE);
    ioapic_entry(1, 0x20 + 1, 0, 0);
    kstatus(STATUS_OK, "Keyboard initialized\n");
}
