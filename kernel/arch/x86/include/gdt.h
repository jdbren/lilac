#ifndef __GDT_H
#define __GDT_H

#include <asm/gdt.h>

#define GDT_ENTRY(base, limit, access, flags) \
( \
    (limit & 0xff)        << 8 * 0 | \
    (limit >> 8 & 0xff)   << 8 * 1 | \
    (limit >> 16 & 0x0f)  << 8 * 6 | \
    (base & 0xff)         << 8 * 2 | \
    (base >> 8 & 0xff)    << 8 * 3 | \
    (base >> 16 & 0xff)   << 8 * 4 | \
    (base >> 24 & 0xff)   << 8 * 7 | \
    (access)              << 8 * 5 | \
    (flags << 4)          << 8 * 6   \
)

void gdt_init(void);
void set_tss_esp0(uintptr_t esp0);

#endif
