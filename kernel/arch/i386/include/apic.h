#ifndef _APIC_H
#define _APIC_H

#include <kernel/types.h>

void lapic_enable(uintptr_t apic_base);
void io_apic_init(struct ioapic *ioapic, struct int_override *over, u8 num_over);
void io_apic_entry(u8 irq, u8 vector, u8 flags, u8 dest);
void apic_eoi(void);
void ap_init(u8 numcores);

#endif
