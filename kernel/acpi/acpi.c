#include <acpi/acpi.h>
#include <string.h>
#include <stdbool.h>
#include <kernel/panic.h>
#include <acpi/madt.h>
#include <mm/kheap.h>
#include <paging.h>
#include <asm/io.h>

static bool is_valid(struct SDTHeader *addr)
{
    u8 check = 0;
    for (int i = 0; i < addr->Length; i++)
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
    for (int i = 0; i < sizeof(*rsdp); i++)
        check += ((char *)rsdp)[i];
    if ((u8)(check) != 0)
        kerror("Checksum is incorrect\n");

    map_page((void*)(rsdp->RsdtAddress & 0xfffff000), 
            (void*)(rsdp->RsdtAddress & 0xfffff000), 0x1);

    read_rsdt((struct SDTHeader*)rsdp->RsdtAddress, info);
}
