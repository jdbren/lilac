// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/types.h>
#include <lilac/config.h>
#include "cpufaults.h"
#include "idt.h"
#include "pic.h"
#include "asm/segments.h"

#define IDT_SIZE 256
#define DPL_3 0x60

typedef struct IDTGate {
    u16 offset_low;
    u16 selector;
    u8 zero;
    u8 type_attr;
    u16 offset_high;
} __packed idt_entry_t;

struct IDT {
    u16 size;
    u32 offset;
} __packed;


static idt_entry_t idt_entries[IDT_SIZE];
static struct IDT idtptr = {
    sizeof(idt_entry_t) * IDT_SIZE - 1,
    (u32)&idt_entries
};

void idt_init(void)
{
    extern void syscall_handler(void);
    idt_entry(0, (u32)div0, __KERNEL_CS, TRAP_GATE);
    idt_entry(6, (u32)invldop, __KERNEL_CS, TRAP_GATE);
    idt_entry(8, (u32)dblflt, __KERNEL_CS, TRAP_GATE);
    idt_entry(13, (u32)gpflt, __KERNEL_CS, TRAP_GATE);
    idt_entry(14, (u32)pgflt, __KERNEL_CS, TRAP_GATE);
    idt_entry(0x80, (u32)syscall_handler, __KERNEL_CS, INT_GATE | DPL_3);

    pic_initialize();

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
} __packed;

struct stack_state {
    unsigned int error_code;
    unsigned int eip;
    unsigned int cs;
    unsigned int eflags;
} __packed;
