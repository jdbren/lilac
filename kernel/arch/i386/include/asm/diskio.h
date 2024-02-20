#ifndef x86_DISKIO_H
#define x86_DISKIO_H

#include <kernel/types.h>

void disk_read(u32 lba, u32 sector_count, u32 buffer);
//void disk_write(u32 lba, u32 sector_count, u32 buffer);

#endif
