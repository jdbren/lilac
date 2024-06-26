// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef x86_APIC_H
#define x86_APIC_H

#include <lilac/types.h>
#include <acpi/madt.h>

void apic_init(struct madt_info *madt);
void lapic_enable(uintptr_t apic_base);
void ioapic_init(struct ioapic *ioapic, struct int_override *over, u8 num_over);
void ioapic_entry(u8 irq, u8 vector, u8 flags, u8 dest);
void apic_eoi(void);
int ap_init(u8 numcores);

#endif
