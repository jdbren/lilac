#include <string.h>
#include <drivers/blkdev.h>
#include <lilac/fs.h>
#include <mm/kheap.h>
#include <fs/mbr.h>
#include <fs/gpt.h>

#define MAX_DISKS 8

static struct gendisk *disks[MAX_DISKS];
static int num_disks;

static int __must_check get_part_type(struct gendisk *disk, struct gpt_part_entry *part)
{
    unsigned char buf[512];
    disk->ops->disk_read(disk, part->starting_lba, buf, 1);
    if (buf[510] == 0x55 && buf[511] == 0xAA) {
        return MSDOS;
    }
    return -1;
}

int __must_check add_gendisk(struct gendisk *disk)
{
    if (num_disks >= MAX_DISKS)
        return -1;
    disks[num_disks++] = disk;
    return 0;
}

int scan_partitions(struct gendisk *disk)
{
    printf("Scanning partitions\n");
    printf("Driver: %s\n", disk->driver);
    char *buf = kzmalloc(512);
    const struct MBR *mbr;
	const struct mbr_part_entry *mbr_part;
    const struct GPT *gpt;
    const struct gpt_part_entry *gpt_part;
    int status;

    disk->ops->disk_read(disk, 0, buf, 1);
    mbr = (struct MBR*)buf;
    if (mbr->signature != 0xAA55) {
		printf("Invalid MBR signature\n");
        return -1;
	}

    mbr_part = &mbr->partition_table[0];
    if (mbr_part->type != 0xEE) {
		// Not yet implemented
		// mbr_read(mbr);
        return -1;
	} else {
    	printf("GPT found\n");
        disk->ops->disk_read(disk, 1, buf, 1);
		if (gpt_validate((struct GPT*)buf)) {
			printf("GPT invalid\n");
            return -1;
		}
        for (int i = 0; i < 1; i ++) {
            disk->ops->disk_read(disk, i+2, buf, 1);
            gpt_part = (struct gpt_part_entry*)buf;
            for (int j = 0; j < 4; j++, gpt_part++) {
                if (gpt_part->starting_lba == 0)
                    continue;
                status = create_block_dev(disk, gpt_part);
                if (status) {
                    printf("Invalid partition found at lba %d\n",
                        gpt_part->starting_lba);
                }
            }
        }
	}

    return 0;
}

int __must_check create_block_dev(struct gendisk *disk,
    struct gpt_part_entry *part_entry)
{
    struct block_device *bdev = kzmalloc(sizeof(*bdev));

    // Identify fs type
    enum fs_type type = get_part_type(disk, part_entry);
    if (type < 0) {
        kfree(bdev);
        printf("Invalid partition type\n");
        return -1;
    }

    // Initialize block device
    bdev->first_sector = part_entry->starting_lba;
    bdev->num_sectors = part_entry->ending_lba - part_entry->starting_lba;
    bdev->disk = disk;
    if (disk->partitions == NULL) {
        disk->partitions = bdev;
    } else {
        struct block_device *tmp = disk->partitions;
        while (tmp->next)
            tmp = tmp->next;
        tmp->next = bdev;
    }

    // Register with vfs
    fs_mount(bdev, type);

    return 0;
}
