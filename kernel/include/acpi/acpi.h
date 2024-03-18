// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _ACPI_H
#define _ACPI_H

#include <kernel/types.h>

struct RSDP {
    char Signature[8];
    u8 Checksum;
    char OEMID[6];
    u8 Revision;
    u32 RsdtAddress;
} __attribute__((packed));

struct XSDP {
    char Signature[8];
    u8 Checksum;
    char OEMID[6];
    u8 Revision;
    u32 RsdtAddress;      // deprecated since v2.0

    u32 Length;
    u64 XsdtAddress;
    u8 ExtendedChecksum;
    u8 reserved[3];
} __attribute__((packed));

struct SDTHeader {
    char Signature[4];
    u32 Length;
    u8 Revision;
    u8 Checksum;
    char OEMID[6];
    char OEMTableID[8];
    u32 OEMRevision;
    u32 CreatorID;
    u32 CreatorRevision;
} __attribute__((packed));

struct acpi_info {
    struct madt_info *madt;
    struct hpet_info *hpet;
    // struct facp_info *facp;
};

void parse_acpi(struct RSDP *rsdp, struct acpi_info *info);
int acpi_full_init(void);
void scan_sys_bus(void);

#endif
