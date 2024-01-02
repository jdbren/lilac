#ifndef KERNEL_KEYBOARD_H
#define KERNEL_KEYBOARD_H

#include <stdint.h>

#define KEYBOARD_DATA_PORT 0x60

uint8_t keyboard_read(void);

void keyboard_initialize(void);
void keyboard_interrupt(void);

#endif
