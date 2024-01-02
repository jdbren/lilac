#include <stdio.h>

#include <kernel/idt.h>
#include <kernel/pic.h>
#include <kernel/keyboard.h>
#include <kernel/keymap.h>

#include "io.h"

extern void keyboard_handler(void);

uint8_t keyboard_read(void) {
    return inb(KEYBOARD_DATA_PORT);
}

void keyboard_initialize(void) {
    idt_entry(0x20 + 1, (uint32_t)keyboard_handler, 0x08, INT_GATE);
    IRQ_clear_mask(1);
    printf("Keyboard initialized\n");
}

void keyboard_interrupt(void) {
    int8_t keycode;

    keycode = inb(KEYBOARD_DATA_PORT);
    /* Only print characters on keydown event that have
     * a non-zero mapping */
    if(keycode >= 0 && keyboard_map[keycode]) {
        printf("%c", keyboard_map[keycode]);
    }

    /* Send End of Interrupt (EOI) to master PIC */
    pic_sendEOI(1);
}
