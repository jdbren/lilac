#ifndef _APIC_H
#define _APIC_H

#include <kernel/types.h>

void enable_apic(void);
void io_apic(uintptr_t base);
void io_apic_entry(uint8_t irq, uint8_t vector, uint8_t flags, uint8_t dest);
void apic_eoi(void);

#endif
