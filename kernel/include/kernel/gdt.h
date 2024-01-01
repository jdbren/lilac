#ifndef _GDT_H
#define _GDT_H

#include <stdint.h>

typedef struct GDTEntry {
    uint32_t base;
    uint32_t limit;
    uint8_t access_byte;
    uint8_t flags;
} __attribute__((packed)) GDTEntry;

typedef struct GDT {
    uint16_t size;
    uint32_t offset;
} __attribute__((packed)) GDT;

void gdt_initialize(void);
void gdt_entry(uint8_t *target, struct GDTEntry source);

#endif
