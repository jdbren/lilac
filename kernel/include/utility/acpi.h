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

struct MADT {
    struct SDTHeader header;
    u32 LocalApicAddress;
    u32 Flags;
} __attribute__((packed));

struct MADTEntry {
    u8 Type;
    u8 Length;
} __attribute__((packed));

void read_rsdp(struct RSDP *rsdp);

#endif
