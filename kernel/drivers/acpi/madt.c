// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <acpi/madt.h>
#include <string.h>
#include <stdbool.h>
#include <lilac/panic.h>
#include <lilac/types.h>
#include <lilac/log.h>
#include <mm/kmalloc.h>

static bool is_valid(struct SDTHeader *addr)
{
    u8 check = 0;
    for (u32 i = 0; i < addr->Length; i++)
        check += ((char *)addr)[i];
    if ((u8)(check) != 0)
        return false;
    if (memcmp(addr->Signature, "APIC", 4))
        return false;
    return true;
}

static struct ioapic* parse_ioapic(struct MADTEntry *entry)
{
    struct ioapic *info = kzmalloc(sizeof(struct ioapic));
    info->address = ((u32*)entry)[1];
    info->id = ((u8*)entry)[2];
    info->gsi_base = ((u32*)entry)[2];
    return info;
}

static struct int_override* parse_override(struct MADTEntry *entry)
{
    struct int_override *info = kzmalloc(sizeof(struct int_override));
    info->flags = ((u16*)entry)[4];
    info->global_system_interrupt = ((u32*)entry)[1];
    info->bus = ((u8*)entry)[2];
    info->source = ((u8*)entry)[3];

#ifdef DEBUG_APIC
    klog(LOG_DEBUG, "Override: %x %x %x %x\n", info->flags, info->global_system_interrupt, info->bus, info->source);
#endif
    return info;
}

static struct ioapic_nmi* parse_ioapic_nmi(struct MADTEntry *entry)
{
    struct ioapic_nmi *info = kzmalloc(sizeof(struct ioapic_nmi));
    info->global_system_interrupt = *((u32*)((u8*)entry+6));
    info->flags = ((u16*)entry)[2];
    info->source = ((u8*)entry)[2];
    return info;
}

static struct lapic_nmi* parse_lapic_nmi(struct MADTEntry *entry)
{
    struct lapic_nmi *info = kzmalloc(sizeof(struct lapic_nmi));
    info->flags = *(u16*)((u8*)entry+3);
    info->processor = ((u8*)entry)[2];
    info->lint = ((u8*)entry)[5];
    return info;
}

struct madt_info* parse_madt(struct SDTHeader *addr)
{
    if (!is_valid(addr))
        kerror("Invalid MADT\n");

    struct MADT *madt = (struct MADT*)addr;
    struct madt_info *info = kzmalloc(sizeof(struct madt_info));
    info->lapic_addr = madt->LocalApicAddress;

    struct MADTEntry *entry = (struct MADTEntry*)(madt + 1);
    struct ioapic *ioptr;
    struct int_override *intptr;
    struct ioapic_nmi *ionmiptr;
    struct lapic_nmi *lnmiptr;
    for (; (uintptr_t)entry < (uintptr_t)madt + madt->header.Length;
    entry = (struct MADTEntry*)((uintptr_t)entry + entry->Length)) {
        switch (entry->Type) {
            case 0:
                info->core_cnt++;
                break;
            case 1:
                info->ioapic_cnt++;
                if (info->ioapic_cnt > 1) {
                    klog(LOG_WARN, "Multiple IOAPICs not supported\n");
                    info->ioapic_cnt--;
                    continue;
                }
                ioptr = parse_ioapic(entry);
                ioptr->next = info->ioapics;
                info->ioapics = ioptr;
                break;
            case 2:
                info->override_cnt++;
                intptr = parse_override(entry);
                intptr->next = info->int_overrides;
                info->int_overrides = intptr;
                break;
            case 3:
                info->ionmi_cnt++;
                ionmiptr = parse_ioapic_nmi(entry);
                ionmiptr->next = info->ioapic_nmis;
                info->ioapic_nmis = ionmiptr;
                break;
            case 4:
                info->lnmi_cnt++;
                lnmiptr = parse_lapic_nmi(entry);
                lnmiptr->next = info->lapic_nmis;
                info->lapic_nmis = lnmiptr;
                break;
            default:
                kerror("Unknown madt entry\n");
        }
    }
    klog(LOG_INFO, "MADT: %d cores, %d ioapics, %d overrides, %d ioapic nmis, %d lapic nmis\n",
        info->core_cnt, info->ioapic_cnt, info->override_cnt,
        info->ionmi_cnt, info->lnmi_cnt);
    return info;
}

void dealloc_madt(struct madt_info *info)
{
    struct ioapic *ioapic = info->ioapics;
    while (ioapic) {
        struct ioapic *next = ioapic->next;
        kfree(ioapic);
        ioapic = next;
    }
    struct int_override *int_override = info->int_overrides;
    while (int_override) {
        struct int_override *next = int_override->next;
        kfree(int_override);
        int_override = next;
    }
    struct ioapic_nmi *ioapic_nmi = info->ioapic_nmis;
    while (ioapic_nmi) {
        struct ioapic_nmi *next = ioapic_nmi->next;
        kfree(ioapic_nmi);
        ioapic_nmi = next;
    }
    struct lapic_nmi *lapic_nmi = info->lapic_nmis;
    while (lapic_nmi) {
        struct lapic_nmi *next = lapic_nmi->next;
        kfree(lapic_nmi);
        lapic_nmi = next;
    }
    kfree(info);
}
