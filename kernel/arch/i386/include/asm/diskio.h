#ifndef _DISKIO_H
#define _DISKIO_H

#include <kernel/types.h>

void disk_read(uint32_t lba, uint32_t sector_count, uint32_t buffer);
void disk_write(uint32_t lba, uint32_t sector_count, uint32_t buffer);

#endif
