// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef x86_DISKIO_H
#define x86_DISKIO_H

#include <lilac/types.h>

void disk_read(u32 lba, u32 sector_count, u32 buffer);
//void disk_write(u32 lba, u32 sector_count, u32 buffer);

#endif
