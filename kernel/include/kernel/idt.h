#ifndef _IDT_H
#define _IDT_H

#include <stdint.h>

typedef struct IDTGate {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed)) IDTGate;

typedef struct IDT {
    uint16_t size;
    uint32_t offset;
} __attribute__((packed)) IDT;

void idt_initialize(void);

#endif
