#ifndef MBR_H
#define MBR_H

#include <stdint.h>

struct PartitionEntry {
    uint8_t status;
    uint8_t chs_first_sector[3];
    uint8_t type;
    uint8_t chs_last_sector[3];
    uint32_t lba_first_sector;
    uint32_t sector_count;
} __attribute__((packed));

typedef struct MBR {
    uint8_t boot_code[446];
    struct PartitionEntry partition_table[4];
    uint16_t signature;
} __attribute__((packed)) MBR_t;

#endif // MBR_H
