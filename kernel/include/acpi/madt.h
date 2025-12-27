// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef ACPI_MADT_H
#define ACPI_MADT_H

#include <lilac/config.h>
#include <acpi/acpi.h>

struct __packed MADT {
    struct SDTHeader header;
    u32 LocalApicAddress;
    u32 Flags;
};

struct __packed MADTEntry {
    u8 Type;
    u8 Length;
};

struct __packed lapic_entry {
    struct lapic_entry *next;
    u8 acpi_cpu_id;
    u8 apic_id;
    u32 flags;
};

struct ioapic {
    struct ioapic *next;
    u32 address;
    u8 id;
    u8 reserved;
    u8 gsi_base;
};

struct int_override {
    struct int_override *next;
    u16 flags;
    u8 global_system_interrupt;
    u8 bus;
    u8 source;
};

struct ioapic_nmi {
    struct ioapic_nmi *next;
    u32 global_system_interrupt;
    u16 flags;
    u8 source;
};

struct lapic_nmi {
    struct lapic_nmi *next;
    u16 flags;
    u8 processor;
    u8 lint;
};

struct madt_info {
    u32 lapic_addr;
    struct lapic_entry *lapics;
    struct ioapic *ioapics;
    struct int_override *int_overrides;
    struct ioapic_nmi *ioapic_nmis;
    struct lapic_nmi *lapic_nmis;
    u8 core_cnt;
    u8 ioapic_cnt;
    u8 override_cnt;
    u8 ionmi_cnt;
    u8 lnmi_cnt;
};

struct madt_info* parse_madt(struct SDTHeader *addr);
void dealloc_madt(struct madt_info *info);

#endif
