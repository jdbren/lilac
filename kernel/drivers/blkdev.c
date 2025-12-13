#include <lilac/log.h>
#include <lilac/libc.h>
#include <lilac/err.h>
#include <drivers/blkdev.h>
#include <lilac/fs.h>
#include <mm/kmalloc.h>
#include <fs/mbr.h>
#include <fs/gpt.h>

#define MAX_DISKS 8

static struct gendisk *disks[MAX_DISKS];
static int num_disks;
spinlock_t disk_list_lock = SPINLOCK_INIT;

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
static int get_part_type(struct gendisk *disk,
    const struct gpt_part_entry *part)
{
    unsigned char buf[512];
    disk->ops->disk_read(disk, part->starting_lba, buf, 1);
    if (buf[510] == 0x55 && buf[511] == 0xAA) {
        return MSDOS;
    }
    return -1;
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
    char buf[512];
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
                klog(LOG_WARN, "Invalid partition found at lba %d\n",
                    gpt_part->starting_lba);
            }
        }
        disk->state = GD_ADDED;
	}

    return 0;
}

// Look for a block device with the given number
struct block_device *get_bdev(dev_t devnum)
{
    for (int i = 0; i < num_disks; i++) {
        struct block_device *bdev = disks[i]->partitions;
        while (bdev) {
            if (bdev->devnum == devnum)
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
    enum fs_type type = get_part_type(disk, part_entry);
    if (type < 0) {
        kfree(bdev);
        klog(LOG_ERROR, "Invalid partition type\n");
        return -1;
    }

    // Initialize block device
    bdev->devnum = (disk->major << 20) | (disk->first_minor + num);
    bdev->first_sector_lba = part_entry->starting_lba;
    bdev->num_sectors = part_entry->ending_lba - part_entry->starting_lba;
    bdev->disk = disk;
    bdev->type = type;
    if (disk->partitions == NULL) {
        disk->partitions = bdev;
    } else {
        struct block_device *tmp = disk->partitions;
        while (tmp->next)
            tmp = tmp->next;
        tmp->next = bdev;
    }

    // Register with vfs
    // mount_bdev(bdev, type);

    return 0;
}
