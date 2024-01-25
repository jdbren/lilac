#ifndef _DISKIO_H
#define _DISKIO_H

#include <stdint.h>

void disk_read(uint32_t lba, uint32_t sector_count, uint32_t buffer);

#endif
