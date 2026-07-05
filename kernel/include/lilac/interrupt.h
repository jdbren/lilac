#ifndef _KERNEL_INTERRUPT_H
#define _KERNEL_INTERRUPT_H

#include <lilac/types.h>

#define SYSCALL_VECTOR 0x80
#define TLB_SHOOTDOWN_VECTOR 0xF0
#define HALT_CPU_VECTOR 0xFE
#define SPUR_INT_VECTOR 0xFF

#define __isr_handler [[gnu::interrupt]]

int install_isr(int num, void (*handler));
int uninstall_isr(int num);

void arch_eoi(void);
void arch_send_ipi(u8 cpu, u8 vector);
void arch_broadcast_others_ipi(u8 vector);
void arch_broadcast_all_ipi(u8 vector);
void arch_ipi_send_self(u8 vector);
void arch_send_nmi(u8 cpu);
void arch_broadcast_nmi_others(void);

#endif
