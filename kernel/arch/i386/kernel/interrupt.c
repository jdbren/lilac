#include <kernel/interrupt.h>
#include "idt.h"

int install_isr(int num, void (*handler))
{
    idt_entry(num, (u32)handler, 0x08, INT_GATE);
}

int uninstall_isr(int num)
{
    idt_entry(num, 0, 0, 0);
}