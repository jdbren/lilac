// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <kernel/panic.h>
#include <drivers/pci.h>
#include <acpi/acpi.h>
#include <acpi/madt.h>
#include <acpi/hpet.h>
#include <mm/kmm.h>
#include <mm/kheap.h>
#include "acpica/acpi.h"

static bool doChecksum(struct SDTHeader *tableHeader)
{
    unsigned char sum = 0;
    for (int i = 0; i < tableHeader->Length; i++)
        sum += ((char *) tableHeader)[i];
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

    u64 *other_entries = (u64*)((u32)xsdt + sizeof(*xsdt));
    for (int i = 0; i < entries; i++) {
        struct SDTHeader *h = (struct SDTHeader*)other_entries[i];
        map_to_self((void*)((u32)h & 0xFFFFF000), 0x1000, 0x1);
        char sig[5];
        strncpy(sig, h->Signature, 4);
        sig[4] = 0;
        if (!memcmp(h->Signature, "APIC", 4))
            info->madt = parse_madt(h);
        if (!memcmp(h->Signature, "HPET", 4))
            info->hpet = parse_hpet(h);
        unmap_from_self((void*)((u32)h & 0xFFFFF000), 0x1000);
    }
}

void parse_acpi(struct RSDP *rsdp, struct acpi_info *info)
{
    u8 check = 0;
    for (u32 i = 0; i < sizeof(*rsdp); i++)
        check += ((char *)rsdp)[i];
    if ((u8)(check) != 0)
        kerror("Checksum is incorrect\n");

    if (rsdp->Revision == 2) {
        struct XSDP *xsdp = (struct XSDP*)rsdp;
        check = 0;
        for (u32 i = 0; i < sizeof(*xsdp); i++)
            check += ((char *)xsdp)[i];
        if ((u8)(check) != 0)
            kerror("Checksum is incorrect\n");

        map_to_self((void*)((u32)(xsdp->XsdtAddress) & 0xFFFFF000), 0x1000, 0x1);
        read_xsdt((struct SDTHeader*)((u32)(xsdp->XsdtAddress)), info);
        unmap_from_self((void*)((u32)(xsdp->XsdtAddress) & 0xFFFFF000), 0x1000);
    }

    //read_rsdt((struct SDTHeader*)rsdp->RsdtAddress, info);
}


int FullAcpiInit(void)
{
    ACPI_STATUS status;

    printf("Initializing ACPICA...\n");
    status = AcpiInitializeSubsystem();
    if (ACPI_FAILURE(status)) {
        printf("Error initializing ACPICA\n");
        return status;
    }

    printf("ACPICA subsystem initialized\n");

    status = AcpiInitializeTables(NULL, 16, FALSE);
    if (ACPI_FAILURE(status)) {
        printf("Error initializing tables\n");
        return status;
    }

    printf("ACPICA tables initialized\n");

    status = AcpiLoadTables();
    if (ACPI_FAILURE(status)) {
        printf("Error loading tables\n");
        return status;
    }

    printf("ACPICA tables loaded\n");

    status = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
    if (ACPI_FAILURE(status)) {
        printf("Error enabling subsystem\n");
        return status;
    }

    printf("ACPICA subsystem enabled\n");

    status = AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);
    if (ACPI_FAILURE(status)) {
        printf("Error initializing objects\n");
        return status;
    }

    printf("ACPICA objects initialized\n");

    printf("ACPICA fully initialized\n");
    return status;
}

ACPI_STATUS DisplayOneDevice(ACPI_HANDLE ObjHandle, UINT32 Level,
    void *Context, void **ReturnValue)
{
    ACPI_STATUS Status;
    ACPI_DEVICE_INFO *Info = kzmalloc(sizeof(*Info));
    ACPI_BUFFER Path;
    char Buffer[256];
    Path.Length = sizeof(Buffer);
    Path.Pointer = Buffer;


    /* Get the full path of this device and print it */
    Status = AcpiGetName(ObjHandle, ACPI_FULL_PATHNAME, &Path);
    if (ACPI_SUCCESS (Status))
        printf("%s\n", Path.Pointer);
    /* Get the device info for this device and print it */
    Status = AcpiGetObjectInfo(ObjHandle, &Info);

    if (ACPI_SUCCESS (Status)) {
        pcie_read_device(Info);
    }
    kfree(Info);
    *ReturnValue = NULL;
    return AE_OK;
}


void ScanSystemBus(void)
{
    ACPI_TABLE_MCFG *mcfg;
    ACPI_STATUS status = AcpiGetTable("MCFG", 0, (ACPI_TABLE_HEADER**)&mcfg);

    if (ACPI_SUCCESS(status)) {
        pcie_add_map(mcfg);
    }

    ACPI_HANDLE SysBusHandle;
 	AcpiGetHandle(0, "\\", &SysBusHandle);

 	printf("System Bus Devices:\n");
 	AcpiWalkNamespace(ACPI_TYPE_DEVICE, SysBusHandle, INT_MAX,
 		DisplayOneDevice, NULL, NULL, NULL);


}
