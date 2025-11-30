// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <drivers/keyboard.h>
#include <asm/segments.h>
#include <lilac/log.h>
#include "idt.h"
#include "io.h"
#include "apic.h"

#define KEYBOARD_DATA_PORT 0x60

extern void kbd_handler(void);

void keyboard_int(void)
{
    kb_code(inb(KEYBOARD_DATA_PORT));
}

void kbd_int_init(void)
{
    idt_entry(0x20 + 1, (uintptr_t)kbd_handler, __KERNEL_CS, 0, INT_GATE);
    ioapic_entry(1, 0x20 + 1, 0, 0);
    kstatus(STATUS_OK, "Keyboard interrupt handler initialized\n");
}
