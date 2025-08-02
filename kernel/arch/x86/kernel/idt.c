// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/types.h>
#include <lilac/config.h>
#include "cpufaults.h"
#include "idt.h"
#include "pic.h"
#include <asm/segments.h>

#define IDT_SIZE 256
#define DPL_3 0x60

struct __packed IDTGate32 {
    u16 offset_low;
    u16 selector;
    u8 zero;
    u8 type_attr;
    u16 offset_high;
};

struct __packed IDTGate64 {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 zero;
};

struct __packed IDT {
    u16 size;
    uintptr_t offset;
};

#ifdef __x86_64__
typedef struct IDTGate64 idt_entry_t;
#else
typedef struct IDTGate32 idt_entry_t;
#endif

static idt_entry_t idt_entries[IDT_SIZE];
static struct IDT idtptr = {
    sizeof(idt_entry_t) * IDT_SIZE - 1,
    (uintptr_t)&idt_entries
};

inline void load_idt(void)
{
    asm volatile("lidt %0" : : "m"(idtptr));
}

void idt_init(void)
{
    extern void syscall_handler(void);
    idt_entry(0, (uintptr_t)div0, __KERNEL_CS, 0, TRAP_GATE);
    idt_entry(6, (uintptr_t)invldop, __KERNEL_CS, 0, TRAP_GATE);
    idt_entry(8, (uintptr_t)dblflt, __KERNEL_CS, 0, TRAP_GATE);
    idt_entry(13, (uintptr_t)gpflt, __KERNEL_CS, 0, TRAP_GATE);
    idt_entry(14, (uintptr_t)pgflt, __KERNEL_CS, 0, TRAP_GATE);
    idt_entry(0x80, (uintptr_t)syscall_handler, __KERNEL_CS, 0, INT_GATE | DPL_3);

    pic_initialize();

    load_idt();
}


#ifdef __x86_64__
void idt_entry(int num, uintptr_t offset, u16 selector, u8 ist, u8 attr)
{
    idt_entry_t *target = idt_entries + num;
    target->offset_low = offset & 0xFFFF;
    target->offset_mid = (offset >> 16) & 0xFFFF;
    target->offset_high = (offset >> 32) & 0xFFFFFFFF;
    target->selector = selector;
    target->ist = ist;
    target->zero = 0;
    target->type_attr = attr;
}
#else
void idt_entry(int num, u32 offset, u16 selector, u8 unused, u8 attr)
{
    idt_entry_t *target = idt_entries + num;
    target->offset_low = offset & 0xFFFF;
    target->offset_high = (offset >> 16) & 0xFFFF;
    target->selector = selector;
    target->zero = 0;
    target->type_attr = attr;
}
#endif
