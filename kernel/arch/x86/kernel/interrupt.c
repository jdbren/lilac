#include <lilac/interrupt.h>
#include <asm/idt.h>
#include <asm/segments.h>

int install_isr(int num, void (*handler)(void *))
{
    idt_entry(num, (uintptr_t)handler, __KERNEL_CS, 0, INT_GATE);
    return 0;
}

int uninstall_isr(int num)
{
    idt_entry(num, 0, 0, 0, 0);
    return 0;
}
