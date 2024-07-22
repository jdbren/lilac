// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef x86_IDT_H
#define x86_IDT_H

#include <lilac/types.h>

#define INT_GATE 0x8E
#define TRAP_GATE 0x8F
#define TASK_GATE 0x85

struct interrupt_frame {
    u32 ip;
    u32 cs;
    u32 flags;
    u32 sp;
    u32 ss;
};

void idt_init(void);
void idt_entry(int num, u32 offset, u16 selector, u8 attr);
void enable_interrupts(void);

#endif
