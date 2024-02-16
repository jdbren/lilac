#ifndef x86_IDT_H
#define x86_IDT_H

#include <kernel/types.h>

#define INT_GATE 0x8E
#define TRAP_GATE 0x8F
#define TASK_GATE 0x85

void idt_init(void);
void idt_entry(int num, u32 offset, u16 selector, u8 attr);
void enable_interrupts(void);

#endif
