#include <lilac/interrupt.h>
#include "idt.h"

int install_isr(int num, void (*handler))
{
    idt_entry(num, (uintptr_t)handler, 0x08, 0, INT_GATE);
    return 0;
}

int uninstall_isr(int num)
{
    idt_entry(num, 0, 0, 0, 0);
    return 0;
}
