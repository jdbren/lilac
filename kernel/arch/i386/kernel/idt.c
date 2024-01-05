#include <stdint.h>
#include <stdio.h>

#include <kernel/idt.h>
#include <kernel/pic.h>

#define IDT_SIZE 256

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

IDT idtptr;
IDTGate idt_entries[IDT_SIZE];

extern void div0(void);
extern void page_fault(void);

void idt_initialize(void) {
    idtptr.size = (sizeof(IDTGate) * IDT_SIZE) - 1;
    idtptr.offset = (uint32_t)&idt_entries;

    idt_entry(0, (uint32_t)div0, 0x08, TRAP_GATE);
    idt_entry(14, (uint32_t)page_fault, 0x08, TRAP_GATE);

    // Remap the PIC - all disabled initially
    pic_initialize();
    printf("Remapped PIC\n");

    asm volatile("lidt %0" : : "m"(idtptr));
    printf("Loaded IDT\n");
}

void idt_entry(int num, uint32_t offset, uint16_t selector, uint8_t attr) {
    IDTGate *target = idt_entries + num;
    target->offset_low = offset & 0xFFFF;
    target->offset_high = (offset >> 16) & 0xFFFF;
    target->selector = selector;
    target->zero = 0;
    target->type_attr = attr;
}


struct cpu_state {
    unsigned int eax;
    unsigned int ebx;
    unsigned int ecx;
    //...
    unsigned int esp;
} __attribute__((packed));

struct stack_state {
    unsigned int error_code;
    unsigned int eip;
    unsigned int cs;
    unsigned int eflags;
} __attribute__((packed));

void interrupt_handler(struct cpu_state cpu, struct stack_state stack, unsigned int interrupt);
