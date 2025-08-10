// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef x86_IDT_H
#define x86_IDT_H

#include <lilac/types.h>

#define INT_GATE  0x8E
#define TRAP_GATE 0x8F
#define TASK_GATE 0x85

struct interrupt_frame {
    uintptr_t ip;
    uintptr_t cs;
    uintptr_t flags;
    uintptr_t sp;
    uintptr_t ss;
};

void idt_init(void);
void load_idt(void);
void idt_entry(int num, uintptr_t offset, u16 selector, u8 ist_or_unused, u8 attr);

#endif
