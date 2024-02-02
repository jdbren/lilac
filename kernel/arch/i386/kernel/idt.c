#include <kernel/types.h>
#include <asm/cpufaults.h>
#include <idt.h>
#include <pic.h>

#define IDT_SIZE 256

typedef struct IDTGate {
    u16 offset_low;
    u16 selector;
    u8 zero;
    u8 type_attr;
    u16 offset_high;
} __attribute__((packed)) idt_entry_t;

struct IDT {
    u16 size;
    u32 offset;
} __attribute__((packed));


idt_entry_t idt_entries[IDT_SIZE];
struct IDT idtptr = {sizeof(idt_entry_t) * IDT_SIZE - 1, (u32)&idt_entries};

void enable_interrupts(void)
{
    asm volatile("sti");
    printf("Interrupts enabled\n");
}

void idt_initialize(void)
{
    idt_entry(0, (u32)div0, 0x08, TRAP_GATE);
    idt_entry(6, (u32)invldop, 0x08, TRAP_GATE);
    idt_entry(13, (u32)gpflt, 0x08, TRAP_GATE);
    idt_entry(14, (u32)pgflt, 0x08, TRAP_GATE);

    // Remap the PIC
    pic_initialize();
    printf("Remapped PIC\n");

    asm volatile("lidt %0" : : "m"(idtptr));
    printf("Loaded IDT\n");
}

void idt_entry(int num, u32 offset, u16 selector, u8 attr)
{
    idt_entry_t *target = idt_entries + num;
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
