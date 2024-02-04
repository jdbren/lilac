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

#endif