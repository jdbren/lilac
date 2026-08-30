#include <drivers/blkdev.h>

#include <lilac/log.h>
#include <lilac/libc.h>
#include <lilac/err.h>
#include <lilac/fs.h>
#include <lilac/math.h>
#include <mm/kmalloc.h>
#include <mm/page.h>
#include <mm/kmm.h>
#include <fs/mbr.h>
#include <fs/gpt.h>
#include <lib/log_base2.h>

#define MAX_DISKS 8

static struct gendisk *disks[MAX_DISKS];
static int num_disks;
spinlock_t disk_list_lock = SPINLOCK_INIT;

static const struct gpt_guid gpt_part_guids[] = {
    [GPT_UNUSED] = {{
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    }},

    [GPT_MBR_SCHEME] = {{
        0x41, 0xEE, 0x4D, 0x02,
        0xE7, 0x33,
        0xD3, 0x11,
        0x9D, 0x69, 0x00, 0x08,
        0xC7, 0x81, 0xF3, 0x9F,
    }},

    [GPT_EFI_SYSTEM] = {{
        0x28, 0x73, 0x2A, 0xC1,
        0x1F, 0xF8,
        0xD2, 0x11,
        0xBA, 0x4B, 0x00, 0xA0,
        0xC9, 0x3E, 0xC9, 0x3B,
    }},

    [GPT_BIOS_BOOT] = {{
        0x48, 0x61, 0x68, 0x21,
        0x49, 0x64,
        0x6F, 0x6E,
        0x74, 0x4E, 0x65, 0x65,
        0x64, 0x45, 0x46, 0x49,
    }},

    [GPT_MICROSOFT_BASIC_DATA] = {{
        0xA2, 0xA0, 0xD0, 0xEB,
        0xE5, 0xB9,
        0x33, 0x44,
        0x87, 0xC0, 0x68, 0xB6,
        0xB7, 0x26, 0x99, 0xC7,
    }},

    [GPT_LINUX_FILESYSTEM_DATA] = {{
        0xAF, 0x3D, 0xC6, 0x0F,
        0x83, 0x84,
        0x72, 0x47,
        0x8E, 0x79, 0x3D, 0x69,
        0xD8, 0x47, 0x7D, 0xE4,
    }},
};

__maybe_unused
static void uuid_to_str(const u8 *uuid, char *str)
{
    sprintf(str,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0], uuid[1], uuid[2], uuid[3],
        uuid[4], uuid[5],
        uuid[6], uuid[7],
        uuid[8], uuid[9],
        uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]
    );
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';

    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;

    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    return -1;
}

bool gpt_uuid_parse(const char *str, uint8_t out[16])
{
    static const uint8_t map[16] = {
        3, 2, 1, 0,       /* data1: uint32_t LE */
        5, 4,             /* data2: uint16_t LE */
        7, 6,             /* data3: uint16_t LE */
        8, 9,             /* remaining bytes unchanged */
        10, 11, 12, 13,
        14, 15
    };

    uint8_t uuid[16];
    int byte_index = 0;

    for (int i = 0; i < 36; ) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (str[i] != '-')
                return false;

            i++;
            continue;
        }

        int high = hex_value(str[i]);
        int low = hex_value(str[i + 1]);

        if (high < 0 || low < 0)
            return false;

        uuid[byte_index++] = (high << 4) | low;
        i += 2;
    }

    if (str[36] != '\0')
        return false;

    for (int i = 0; i < 16; i++)
        out[map[i]] = uuid[i];

    return true;
}

__must_check
static int create_block_dev(struct gendisk *disk,
   const struct gpt_part_entry *part_entry, int num);

int gpt_validate(struct GPT *gpt) {
    if (gpt->signature != GPT_SIGNATURE) {
        return -1;
    }
    return 0;
}

__must_check
static gpt_part_type get_part_type(struct gendisk *disk,
    const struct gpt_part_entry *part)
{
    u8 *type = (u8*)&part->partition_type_guid;
    for (int i = 0; i < sizeof(gpt_part_guids) / sizeof(gpt_part_guids[0]); i++) {
        if (memcmp(type, gpt_part_guids[i].bytes, 16) == 0) {
            return i;
        }
    }
    return GPT_UNKNOWN;
}

__must_check
int add_gendisk(struct gendisk *disk)
{
    if (num_disks >= MAX_DISKS)
        return -1;
    acquire_lock(&disk_list_lock);
    disks[num_disks++] = disk;
    release_lock(&disk_list_lock);
    return 0;
}

int scan_partitions(struct gendisk *disk)
{
    if (disk == NULL) {
        for (int i = 0; i < num_disks; i++) {
            if (disks[i]->state == GD_NEED_PART_SCAN)
                scan_partitions(disks[i]);
        }
        return 0;
    }

    klog(LOG_INFO, "Scanning partitions...\n");
    klog(LOG_INFO, "Driver: %s\n", disk->driver);
    char *buf = kmalloc(512);
    if (!buf) {
        klog(LOG_ERROR, "Failed to allocate memory for partition scan\n");
        return -ENOMEM;
    }
    const struct MBR *mbr;
    const struct mbr_part_entry *mbr_part;
    const struct gpt_part_entry *gpt_part;
    int status;

    disk->ops->disk_read(disk, 0, buf, 1);
    mbr = (struct MBR*)buf;
    if (mbr->signature != 0xAA55) {
        klog(LOG_ERROR, "Invalid MBR signature\n");
        return -1;
    }

    mbr_part = &mbr->partition_table[0];
    if (mbr_part->type != 0xEE) {
        // Not yet implemented
        // mbr_read(mbr);
        klog(LOG_WARN, "MBR partitioning not supported\n");
        return -1;
    } else {
        disk->ops->disk_read(disk, 1, buf, 1);
        if (gpt_validate((struct GPT*)buf)) {
            klog(LOG_ERROR, "GPT invalid\n");
            return -1;
        }
        disk->ops->disk_read(disk, 2, buf, 1);
        gpt_part = (struct gpt_part_entry*)buf;
        for (int j = 0; j < 4; j++, gpt_part++) {
            if (gpt_part->starting_lba == 0)
                continue;
            status = create_block_dev(disk, gpt_part, j);
            if (status) {
                klog(LOG_WARN, "Skipping partition starting at lba %d\n",
                    gpt_part->starting_lba);
            } else {
                klog(LOG_INFO, "Partition %d: LBA %lu - %lu\n", j,
                    gpt_part->starting_lba, gpt_part->ending_lba);
                disk->num_partitions++;
            }
        }
        disk->state = GD_ADDED;
    }

    return 0;
}

// Look for a block device with the given number
struct block_device *get_bdev(int major)
{
    for (int i = 0; i < num_disks; i++) {
        struct block_device *bdev = disks[i]->partitions;
        while (bdev) {
            if ((bdev->devnum >> 20) == major)
                return bdev;
            bdev = bdev->next;
        }
    }
    return NULL;
}

struct block_device * get_bdev_by_uuid(const char *uuid_str)
{
    uint8_t uuid[16];
    if (!gpt_uuid_parse(uuid_str, uuid))
        return NULL;

    for (int i = 0; i < num_disks; i++) {
        struct block_device *bdev = disks[i]->partitions;
        while (bdev) {
            if (memcmp(bdev->uuid, uuid, 16) == 0)
                return bdev;
            bdev = bdev->next;
        }
    }
    return NULL;
}

__must_check
static int create_block_dev(struct gendisk *disk,
    const struct gpt_part_entry *part_entry, int num)
{
    struct block_device *bdev = kzmalloc(sizeof(*bdev));
    if (!bdev) {
        klog(LOG_ERROR, "Failed to allocate block device\n");
        return -ENOMEM;
    }

    // Identify fs type
    gpt_part_type type = get_part_type(disk, part_entry);
    if (type < 0) {
        kfree(bdev);
        klog(LOG_WARN, "Unrecognized partition type\n");
        return -1;
    } else if (type == 0) {
        kfree(bdev);
        klog(LOG_WARN, "Unused partition\n");
        return -1;
    }

    // Initialize block device
    bdev->partition_type = type;
    memcpy(bdev->uuid, part_entry->unique_partition_guid, 16);
    bdev->devnum = (disk->major << 20) | (disk->first_minor + num);
    bdev->first_sector_lba = part_entry->starting_lba;
    // last LBA inclusive
    bdev->num_sectors = part_entry->ending_lba - part_entry->starting_lba + 1;
    set_blocksize(bdev, disk->sector_size);
    bdev->disk = disk;
    if (disk->partitions == NULL) {
        disk->partitions = bdev;
    } else {
        struct block_device *tmp = disk->partitions;
        while (tmp->next)
            tmp = tmp->next;
        tmp->next = bdev;
    }

    return 0;
}

/* assumes size > 256 */
static inline unsigned int blksize_bits(unsigned int size)
{
    return order_base_2(size >> SECTOR_SHIFT) + SECTOR_SHIFT;
}

int set_blocksize(struct block_device *bdev, int size)
{
    /* Size must be a power of two, and between 512 and PAGE_SIZE */
    if (size > PAGE_SIZE || size < 512 || !is_pow_2(size))
        return -EINVAL;

    /* Size cannot be smaller than the size supported by the device */
    if (size < bdev->disk->sector_size)
        return -EINVAL;

    /* Don't change the size if it is same as current */
    if (bdev->blocksize_bits != blksize_bits(size)) {
        // sync_blockdev(bdev);
        bdev->blocksize_bits = blksize_bits(size);
        // kill_bdev(bdev);
    }
    return 0;
}

int sb_set_blocksize(struct super_block *sb, int size)
{
    if (set_blocksize(sb->s_bdev, size))
        return 0;
    /* If we get here, we know size is power of two
     * and it's value is between 512 and PAGE_SIZE */
    sb->s_blocksize = size;
    sb->s_blocksize_bits = blksize_bits(size);
    return sb->s_blocksize;
}

int sb_min_blocksize(struct super_block *sb, int size)
{
    int minsize = sb->s_bdev->disk->sector_size;
    if (size < minsize)
        size = minsize;
    return sb_set_blocksize(sb, size);
}

struct blkio_desc * bread(struct block_device *bdev, u64 block_num, size_t size)
{
    struct gendisk *disk = bdev->disk;
    struct blkio_desc *bio;

    if (size % disk->sector_size != 0) {
        klog(LOG_ERROR, "Read size must be a multiple of sector size\n");
        return NULL;
    }

    unsigned long sector = block_num << (bdev->blocksize_bits - SECTOR_SHIFT);

    bio = kzmalloc(sizeof(struct blkio_desc));
    if (!bio)
        return ERR_PTR(-ENOMEM);

    bio->b_block = block_num;
    bio->b_size = size;
    bio->b_bdev = bdev;
    bio->b_data = get_free_pages(PAGE_UP_COUNT(size), 0);
    if (!bio->b_data) {
        kfree(bio);
        return ERR_PTR(-ENOMEM);
    }
    bio->b_page = virt_to_page(bio->b_data);
    INIT_LIST_HEAD(&bio->b_list);

    long err = disk->ops->disk_read(bdev->disk, bdev->first_sector_lba + sector,
        bio->b_data, size >> SECTOR_SHIFT);

    if (err < 0) {
        bdrop(bio);
        return ERR_PTR(err);
    }

    return bio;
}

int brelease(struct blkio_desc *bio)
{
    if (!bio)
        return 0;
    struct block_device *bdev = bio->b_bdev;
    int ret = 0;
    if (bio->b_state.dirty) {
        struct gendisk *disk = bdev->disk;
        unsigned long sector = bio->b_block << (bdev->blocksize_bits - SECTOR_SHIFT);
        ret = disk->ops->disk_write(bdev->disk, bdev->first_sector_lba + sector,
            bio->b_data, bio->b_size / disk->sector_size);
    }
    free_pages(bio->b_data, PAGE_UP_COUNT(bio->b_size));
    kfree(bio);
    return ret;
}

void bdrop(struct blkio_desc *bio)
{
    if (!bio) return;
    free_pages(bio->b_data, PAGE_UP_COUNT(bio->b_size));
    kfree(bio);
}
