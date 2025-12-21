#ifndef _ATA_H
#define _ATA_H

#include <stdint.h>

#define ATA_DEV_BUSY 0x80
#define ATA_DEV_DRQ 0x08

#define ATA_CMD_READ_DMA_EX 0x25
#define ATA_CMD_WRITE_DMA_EX 0x35
#define ATA_CMD_IDENTIFY 0xEC

#define ATA_ID_SECTOR_SIZE          106
#define ATA_ID_LOGICAL_SECTOR_SIZE  117
#define ATA_ID_LBA28_SECTORS         60
#define ATA_ID_LBA48_SECTORS        100
#define ATA_ID_COMMAND_SETS          83


static inline uint16_t ata_read_id_word(const uint16_t *id_data, int word)
{
    return id_data[word];
}

static inline uint64_t ata_read_id_u64(const uint16_t *id, int start)
{
    return  ((uint64_t)id[start]) |
            ((uint64_t)id[start + 1] << 16) |
            ((uint64_t)id[start + 2] << 32) |
            ((uint64_t)id[start + 3] << 48);
}


static inline uint32_t ata_id_sector_size(const uint16_t *id_data)
{
    uint16_t ss_word = ata_read_id_word(id_data, ATA_ID_SECTOR_SIZE);

    if ((ss_word & (1 << 15)) == 0 && (ss_word & (1 << 0))) {
        uint32_t lss =
            ata_read_id_word(id_data, ATA_ID_LOGICAL_SECTOR_SIZE) |
            (ata_read_id_word(id_data, ATA_ID_LOGICAL_SECTOR_SIZE + 1) << 16);

        if (lss >= 512 && !(lss & (lss - 1)))
            return lss;
    }

    return 512u;
}

static inline uint64_t ata_id_sector_count(const uint16_t *id_data)
{
    if (id_data[ATA_ID_COMMAND_SETS] & (1 << 10)) {
        uint64_t sectors = ata_read_id_u64(id_data,
                                           ATA_ID_LBA48_SECTORS);
        if (sectors)
            return sectors;
    }

    return ((uint32_t)id_data[ATA_ID_LBA28_SECTORS]) |
           ((uint32_t)id_data[ATA_ID_LBA28_SECTORS + 1] << 16);
}

#endif
