// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _MBR_H
#define _MBR_H

#include <lilac/types.h>
#include <lilac/config.h>

struct __packed mbr_part_entry {
    u8 status;
    u8 chs_first_sector[3];
    u8 type;
    u8 chs_last_sector[3];
    u32 lba_first_sector;
    u32 sector_count;
};

struct __packed MBR {
    u8 boot_code[446];
    struct mbr_part_entry partition_table[4];
    u16 signature;
};

void mbr_read(struct MBR *mbr);

#endif // MBR_H
