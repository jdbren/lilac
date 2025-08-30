// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/keyboard.h>

#include <asm/segments.h>
#include <lilac/log.h>
#include <lilac/tty.h>
#include <drivers/framebuffer.h>
#include <utility/keymap.h>
#include "idt.h"
#include "io.h"
#include "apic.h"

unsigned char keyboard_map[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']',
    '\n', /* Enter key */
    0,    /* 29   - Control */
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    /* Left shift */
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0,    /* Right shift */
    '*',
    0,  /* Alt */
    ' ',  /* Space bar */
    0,  /* Caps lock */
    0,  /* 59 - F1 key ... > */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,  /* < ... F10 */
    0,  /* 69 - Num lock*/
    0,  /* Scroll Lock */
    0,  /* Home key */
    0,  /* Up Arrow */
    0,  /* Page Up */
    '-',
    0,  /* Left Arrow */
    0,
    0,  /* Right Arrow */
    '+',
    0,  /* 79 - End key*/
    0,  /* Down Arrow */
    0,  /* Page Down */
    0,  /* Insert Key */
    0,  /* Delete Key */
    0,   0,   0,
    0,  /* F11 Key */
    0,  /* F12 Key */
    0,  /* All other keys are undefined */
};

unsigned char keyboard_map_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}',
    '\n', /* Enter key */
    0,    /* Control */
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '\"', '~',
    0,    /* Left shift */
    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0,    /* Right shift */
    '*',
    0,  /* Alt */
    ' ',  /* Space bar */
    0,  /* Caps lock */
    0,  /* F1 key ... */
    0,   0,   0,   0,   0,   0,   0,   0,
    0,  /* F10 */
    0,  /* Num lock*/
    0,  /* Scroll Lock */
    0,  /* Home key */
    0,  /* Up Arrow */
    0,  /* Page Up */
    '_', /* Some keyboards use a different symbol for shifted '-' */
    0,  /* Left Arrow */
    0,
    0,  /* Right Arrow */
    '+',
    0,  /* End key*/
    0,  /* Down Arrow */
    0,  /* Page Down */
    0,  /* Insert Key */
    0,  /* Delete Key */
    0,   0,   0,
    0,  /* F11 Key */
    0,  /* F12 Key */
    0   /* All other keys undefined */
};

#define KEYBOARD_DATA_PORT 0x60

static volatile u8 key_status_map[256];

#define SHIFT_PRESSED 0x2A
#define SHIFT_RELEASED 0xAA
#define CTRL_PRESSED 0x1D
#define CTRL_RELEASED 0x9D
#define ALT_PRESSED 0x38
#define ALT_RELEASED 0xB8
#define CAPS_LOCK 0x3A



extern void kbd_handler(void);

void keyboard_int(void)
{
    volatile u8 keycode = inb(KEYBOARD_DATA_PORT);

    //printf("Keycode: %x, Status: %x\n", keycode);

    switch (keycode) {
        case SHIFT_PRESSED:
            key_status_map[SHIFT_PRESSED] = KB_SHIFT;
            break;
        case SHIFT_RELEASED:
            key_status_map[SHIFT_PRESSED] = 0;
            break;
        case CTRL_PRESSED:
            key_status_map[CTRL_PRESSED] = KB_CTRL;
            break;
        case CTRL_RELEASED:
            key_status_map[CTRL_PRESSED] = 0;
            break;
        case ALT_PRESSED:
            key_status_map[ALT_PRESSED] = KB_ALT;
            break;
        case ALT_RELEASED:
            key_status_map[ALT_PRESSED] = 0;
            break;
        case CAPS_LOCK:
            key_status_map[CAPS_LOCK] = !key_status_map[CAPS_LOCK];
            break;
    }

    /* Send End of Interrupt (EOI) */
    apic_eoi();

    if (keycode < sizeof keyboard_map && keyboard_map[keycode]) {
        u8 status = key_status_map[SHIFT_PRESSED] | key_status_map[CTRL_PRESSED] | key_status_map[ALT_PRESSED];
        char c;

        if (key_status_map[CAPS_LOCK])
            status |= KB_CAPSLOCK;

        if (status & KB_SHIFT)
            c = keyboard_map_shift[keycode];
        else
            c = keyboard_map[keycode];

        if (status & KB_CTRL)
            c = CTRL(c);

        if (status & KB_ALT)
            c = ALT(c);

        tty_recv_char(c);
    }
}

void keyboard_init(void)
{
    idt_entry(0x20 + 1, (uintptr_t)kbd_handler, __KERNEL_CS, 0, INT_GATE);
    ioapic_entry(1, 0x20 + 1, 0, 0);
    kstatus(STATUS_OK, "Keyboard initialized\n");
}
