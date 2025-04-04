#ifndef _KERNEL_INTERRUPT_H
#define _KERNEL_INTERRUPT_H

#include <lilac/types.h>

#define ISR [[gnu::interrupt]]

int install_isr(int num, void (*handler));
int uninstall_isr(int num);

#endif
