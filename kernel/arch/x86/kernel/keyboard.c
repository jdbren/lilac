// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <drivers/keyboard.h>
#include <lilac/log.h>
#include <lilac/panic.h>
#include <lilac/timer.h>
#include <asm/segments.h>
#include <asm/idt.h>
#include <asm/io.h>
#include <asm/apic.h>

#define KEYBOARD_DATA_PORT 0x60

extern void kbd_handler(void);

void keyboard_int(void)
{
    kb_code(inb(KEYBOARD_DATA_PORT));
}

static inline void kbd_wait(void)
{
    int timeout = 100000;
    while (inb(KEYBOARD_DATA_PORT) != 0xfa && inb(KEYBOARD_DATA_PORT) != 0xfe && --timeout)
        ;
}

void kbd_int_init(void)
{
    u8 resp;
    idt_entry(0x20 + 1, (uintptr_t)kbd_handler, __KERNEL_CS, 0, INT_GATE);
    ioapic_entry(1, 0x20 + 1, 0, 0);

    outb(KEYBOARD_DATA_PORT, 0xFF);   // reset keyboard
    u64 timeout = get_sys_time_ns() + 100000000;
    do {
        resp = inb(KEYBOARD_DATA_PORT);
    } while (resp != 0xfa && resp != 0x55 && resp != 0xaa && get_sys_time_ns() < timeout);
    if (resp != 0xFA && resp != 0xAA) {
        klog(LOG_ERROR, "Keyboard reset failed (response: 0x%02x)\n", resp);
        return;
    }
    do {
        resp = inb(KEYBOARD_DATA_PORT);
    } while (resp != 0xfa && resp != 0x55 && resp != 0xaa);
    if (resp != 0xFA && resp != 0xAA) {
        klog(LOG_ERROR, "Keyboard reset failed (response: 0x%02x)\n", resp);
        return;
    }

    outb(KEYBOARD_DATA_PORT, 0xf0); // set scancode set 1
    kbd_wait();
    outb(KEYBOARD_DATA_PORT, 1);
    kbd_wait();
    outb(KEYBOARD_DATA_PORT, 0xf4); // enable scanning
    kbd_wait();

}
