#include "apic.h"
#include <acpi/madt.h>

void apic_init(struct madt_info *madt)
{
    lapic_enable(madt->lapic_addr);
	ioapic_init(madt->ioapics, madt->int_overrides, madt->override_cnt);
}
