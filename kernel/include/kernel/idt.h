#ifndef KERNEL_IDT_H
#define KERNEL_IDT_H

#include <stdint.h>

#define INT_GATE 0x8E
#define TRAP_GATE 0x8F
#define TASK_GATE 0x85

void idt_initialize(void);
void idt_entry(int num, uint32_t offset, uint16_t selector, uint8_t attr);

static inline void enable_interrupts(void) {
    asm volatile("sti");
    printf("Interrupts enabled\n");
}

#endif
