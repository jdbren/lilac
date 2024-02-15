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
    // struct facp_info *facp;
};

struct acpi_info* parse_acpi(struct RSDP *rsdp);

#endif
