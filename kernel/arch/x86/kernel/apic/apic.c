// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <asm/apic.h>
#include <acpi/madt.h>

void apic_init(struct madt_info *madt)
{
    lapic_enable(madt->lapic_addr);
	ioapic_init(madt->ioapics, madt->int_overrides, madt->override_cnt);
    apic_eoi();
}
