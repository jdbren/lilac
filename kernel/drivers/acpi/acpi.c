// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/log.h>
#include <lilac/boot.h>
#include <lilac/panic.h>
#include <lilac/libc.h>
#include <drivers/pci.h>
#include <acpi/acpi.h>
#include <acpi/madt.h>
#include <acpi/hpet.h>
#include <mm/kmm.h>
#include <mm/kmalloc.h>
#include <mm/page.h>
#include "acpica/acpi.h"

enum ACPI_BRIDGE_TYPE {
    ACPI_BRIDGE_PCI,
    ACPI_BRIDGE_PCIe
};

static bool doChecksum(struct SDTHeader *tableHeader)
{
    unsigned char sum = 0;
    for (u32 i = 0; i < tableHeader->Length; i++)
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

    u32 *other_entries = (u32*)((uintptr_t)rsdt + sizeof(*rsdt));
    for (int i = 0; i < entries; i++) {
        struct SDTHeader *h = (struct SDTHeader*)(uintptr_t)other_entries[i];
        if (!memcmp(h->Signature, "APIC", 4))
            info->madt = parse_madt(h);
    }
}

void read_xsdt(struct SDTHeader *xsdt, struct acpi_info *info)
{
    if (!is_valid(xsdt))
        kerror("Invalid XSDT\n");

    int entries = (xsdt->Length - sizeof(*xsdt)) / 8;

    u64 *other_entries = (u64*)((uintptr_t)xsdt + sizeof(*xsdt));
    for (int i = 0; i < entries; i++) {
        struct SDTHeader *h = (struct SDTHeader*)(uintptr_t)other_entries[i];
        map_to_self((void*)((uintptr_t)h & 0xFFFFF000), 0x1000, 0x1);
        char sig[5];
        strncpy(sig, h->Signature, 4);
        sig[4] = 0;
        if (!memcmp(h->Signature, "APIC", 4))
            info->madt = parse_madt(h);
        if (!memcmp(h->Signature, "HPET", 4))
            info->hpet = parse_hpet(h);
        unmap_from_self((void*)((uintptr_t)h & 0xFFFFF000), 0x1000);
    }
}

void acpi_early_init(void)
{
    struct acpi_info *info = &boot_info.acpi;
    struct RSDP *rsdp = (struct RSDP*)boot_info.mbd.new_acpi->rsdp;
    if (!rsdp)
        kerror("No RSDP found!\n");

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

        klog(LOG_DEBUG, "Found XSDT at %x\n", xsdp->XsdtAddress);

        map_to_self((void*)((uintptr_t)(xsdp->XsdtAddress) & ~0xfff), 0x1000, 0x1);
        read_xsdt((struct SDTHeader*)((uintptr_t)(xsdp->XsdtAddress)), info);
        unmap_from_self((void*)((uintptr_t)(xsdp->XsdtAddress) & ~0xfff), 0x1000);
    } else {
        read_rsdt((struct SDTHeader*)(uintptr_t)rsdp->RsdtAddress, info);
    }

    boot_info.ncpus = info->madt ? info->madt->core_cnt : 1;
}

void acpi_early_cleanup(void)
{
    if (boot_info.acpi.madt)
        dealloc_madt(boot_info.acpi.madt);
    if (boot_info.acpi.hpet)
        dealloc_hpet(boot_info.acpi.hpet);
}


int acpi_subsystem_init(void)
{
    ACPI_STATUS status;

    klog(LOG_INFO, "Initializing ACPICA...\n");
    status = AcpiInitializeSubsystem();
    if (ACPI_FAILURE(status)) {
        klog(LOG_ERROR, "Error initializing ACPI subsystem\n");
        return status;
    }

    status = AcpiInitializeTables(NULL, 16, FALSE);
    if (ACPI_FAILURE(status)) {
        klog(LOG_ERROR, "Error initializing ACPI tables\n");
        return status;
    }

    status = AcpiLoadTables();
    if (ACPI_FAILURE(status)) {
        klog(LOG_ERROR, "Error loading ACPI tables\n");
        return status;
    }

    status = AcpiEnableSubsystem(ACPI_FULL_INITIALIZATION);
    if (ACPI_FAILURE(status)) {
        klog(LOG_ERROR, "Error enabling subsystem\n");
        return status;
    }

    status = AcpiInitializeObjects(ACPI_FULL_INITIALIZATION);
    if (ACPI_FAILURE(status)) {
        klog(LOG_ERROR, "Error initializing objects\n");
        return status;
    }

    kstatus(STATUS_OK, "ACPICA fully initialized\n");
    return status;
}

ACPI_STATUS detect_top_level_device(ACPI_HANDLE ObjHandle, UINT32 Level,
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
    if (ACPI_SUCCESS(Status))
        klog(LOG_DEBUG, "Root device: %s\n", Path.Pointer);

    /* Get the device info for this device */
    Status = AcpiGetObjectInfo(ObjHandle, &Info);

    if (ACPI_SUCCESS(Status) && Info->Valid & ACPI_VALID_HID) {
        if (!strcmp(Info->HardwareId.String, "PNP0A03")) {
            // TODO: PCI init
        }
        if (!strcmp(Info->HardwareId.String, "PNP0A08")) {
            pcie_bus_init(ObjHandle);
        }
    }

    kfree(Info);
    return AE_OK;
}

ACPI_STATUS acpi_print_info(ACPI_HANDLE ObjHandle, UINT32 Level,
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
    if (ACPI_SUCCESS(Status))
        klog(LOG_INFO, "%s\n", Path.Pointer);

    kfree(Info);
    return AE_OK;
}

// TODO: Add PCI support w/ no PCI-E
void scan_sys_bus(void)
{
    ACPI_HANDLE sys_bus_handle;
 	AcpiGetHandle(0, "\\_SB_", &sys_bus_handle);

 	klog(LOG_INFO, "Reading system bus root devices...\n");
 	AcpiWalkNamespace(ACPI_TYPE_DEVICE, sys_bus_handle, 1,
 		detect_top_level_device, NULL, NULL, NULL);
}
