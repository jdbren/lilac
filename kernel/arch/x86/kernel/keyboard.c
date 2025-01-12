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
static volatile u8 key_status_map[0xFF];

#define SHIFT_PRESSED 0x2A
#define SHIFT_RELEASED 0xAA
#define CTRL_PRESSED 0x1D
#define CTRL_RELEASED 0x9D
#define ALT_PRESSED 0x38
#define ALT_RELEASED 0xB8
#define CAPS_LOCK 0x3A

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
    u8 keycode;

    keycode = inb(KEYBOARD_DATA_PORT);

    /* Send End of Interrupt (EOI) */
    apic_eoi();

    switch (keycode) {
        case SHIFT_PRESSED:
            key_status_map[SHIFT_PRESSED] = 1;
            break;
        case SHIFT_RELEASED:
            key_status_map[SHIFT_PRESSED] = 0;
            break;
        case CTRL_PRESSED:
            key_status_map[CTRL_PRESSED] = 1;
            break;
        case CTRL_RELEASED:
            key_status_map[CTRL_PRESSED] = 0;
            break;
        case ALT_PRESSED:
            key_status_map[ALT_PRESSED] = 1;
            break;
        case ALT_RELEASED:
            key_status_map[ALT_PRESSED] = 0;
            break;
        case CAPS_LOCK:
            key_status_map[CAPS_LOCK] = !key_status_map[CAPS_LOCK];
            break;
    }

    if (keycode >= 0 && keycode < sizeof keyboard_map && keyboard_map[keycode]) {
        char c = keyboard_map[keycode];
        if (key_status_map[SHIFT_PRESSED])
            c = c - 32;
        if (console) console_intr(c);
        else graphics_putchar(c);
    }
}

void keyboard_init(void)
{
    idt_entry(0x20 + 1, (u32)keyboard_int, 0x08, INT_GATE);
    ioapic_entry(1, 0x20 + 1, 0, 0);
    kstatus(STATUS_OK, "Keyboard initialized\n");
}
