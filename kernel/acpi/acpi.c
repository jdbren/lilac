// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
// Copyright (C) 2024 Jackson Brenneman
// GPL v3.0 
#include <string.h>
#include <stdbool.h>
#include <kernel/panic.h>
#include <acpi/acpi.h>
#include <acpi/madt.h>
#include <mm/kmm.h>
#include <mm/kheap.h>

static bool is_valid(struct SDTHeader *addr)
{
    u8 check = 0;
    for (u32 i = 0; i < addr->Length; i++)
        check += ((char *)addr)[i];
    if ((u8)(check) != 0)
        return false;
    if (memcmp(addr->Signature, "RSDT", 4))
        return false;
    return true;
}

void read_rsdt(struct SDTHeader *rsdt, struct acpi_info *info)
{
    if (!is_valid(rsdt))
        kerror("Invalid RSDT\n");

    int entries = (rsdt->Length - sizeof(*rsdt)) / 4;

    u32 *other_entries = (u32*)((u32)rsdt + sizeof(*rsdt));
    for (int i = 0; i < entries; i++) {
        struct SDTHeader *h = (struct SDTHeader*)other_entries[i];
        if (!memcmp(h->Signature, "APIC", 4))
            info->madt = parse_madt(h);
    }
}

void parse_acpi(struct RSDP *rsdp, struct acpi_info *info)
{
    u8 check = 0;
    for (u32 i = 0; i < sizeof(*rsdp); i++)
        check += ((char *)rsdp)[i];
    if ((u8)(check) != 0)
        kerror("Checksum is incorrect\n");

    map_to_self((void*)(rsdp->RsdtAddress & 0xfffff000), 0x1);

    read_rsdt((struct SDTHeader*)rsdp->RsdtAddress, info);
}
