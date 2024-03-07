// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <string.h>
#include <stdbool.h>
#include <kernel/panic.h>
#include <acpi/acpi.h>
#include <acpi/madt.h>
#include <mm/kmm.h>
#include <mm/kheap.h>

static bool doChecksum(struct SDTHeader *tableHeader)
{
    unsigned char sum = 0;

    for (int i = 0; i < tableHeader->Length; i++)
        sum += ((char *) tableHeader)[i];

    printf("Checksum: %d\n", sum);

    return sum == 0;
}

static bool is_valid(struct SDTHeader *addr)
{
    if (!doChecksum(addr))
        return false;
    if (!memcmp(addr->Signature, "RSDT", 4) ||
        !memcmp(addr->Signature, "XSDT", 4))
        return true;
    return false;
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

void read_xsdt(struct SDTHeader *xsdt, struct acpi_info *info)
{
    if (!is_valid(xsdt))
        kerror("Invalid XSDT\n");

    int entries = (xsdt->Length - sizeof(*xsdt)) / 8;
    printf("Entries: %d\n", entries);
    printf("XSDT: %x\n", xsdt);

    u64 *other_entries = (u64*)((u32)xsdt + sizeof(*xsdt));
    printf("Other entries: %x\n", other_entries);
    for (int i = 0; i < entries; i++) {
        struct SDTHeader *h = (struct SDTHeader*)other_entries[i];
        map_to_self((void*)((u32)h & 0xFFFFF000), 0x1);
        char sig[5];
        if (!memcmp(h->Signature, "APIC", 4))
            info->madt = parse_madt(h);
        strncpy(sig, h->Signature, 4);
        sig[4] = 0;
        printf("Signature: %s\n", sig);
    }
}

void parse_acpi(struct RSDP *rsdp, struct acpi_info *info)
{
    char sig[9];
    char oemid[7];

    u8 check = 0;
    for (u32 i = 0; i < sizeof(*rsdp); i++)
        check += ((char *)rsdp)[i];
    if ((u8)(check) != 0)
        kerror("Checksum is incorrect\n");

    strncpy(sig, rsdp->Signature, 8);
    strncpy(oemid, rsdp->OEMID, 6);
    sig[8] = 0;
    oemid[6] = 0;

    printf("revision: %d\n", rsdp->Revision);
    printf("Signature: %s\n", sig);
    printf("OEMID: %s\n", oemid);

    if (rsdp->Revision == 2) {
        struct XSDP *xsdp = (struct XSDP*)rsdp;
        check = 0;
        for (u32 i = 0; i < sizeof(*xsdp); i++)
            check += ((char *)xsdp)[i];
        if ((u8)(check) != 0)
            kerror("Checksum is incorrect\n");

        printf("XSDT Length: %d\n", xsdp->Length);
        printf("XSDT XsdtAddress: %x\n", xsdp->XsdtAddress);

        map_to_self((void*)((u32)(xsdp->XsdtAddress) & 0xFFFFF000), 0x1);

        read_xsdt((struct SDTHeader*)((u32)(xsdp->XsdtAddress)), info);
    }

    //read_rsdt((struct SDTHeader*)rsdp->RsdtAddress, info);
}
