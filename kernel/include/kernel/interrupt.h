#ifndef _KERNEL_INTERRUPT_H
#define _KERNEL_INTERRUPT_H

#include <kernel/types.h>

#define ISR __attribute__((interrupt))

int install_isr(int num, void (*handler));
int uninstall_isr(int num);

#endif
