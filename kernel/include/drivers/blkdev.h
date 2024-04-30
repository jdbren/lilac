#ifndef _BLKDEV_H
#define _BLKDEV_H

#include <lilac/types.h>
#include <lilac/config.h>
#include <lilac/sync.h>

struct gendisk {
    int major;
    int first_minor;
    char driver[8];
    struct disk_operations *ops;
    struct block_device *partitions;
    struct request_queue *queue;
    void *private;
    spinlock_t lock;
    int state;
#define GD_NEED_PART_SCAN		0
#define GD_READ_ONLY			1
#define GD_DEAD				    2
#define GD_NATIVE_CAPACITY		3
#define GD_ADDED			    4
};

struct disk_operations {
    int (*disk_read)(struct gendisk*, u64 lba, void *, u32 cnt);
    int (*disk_write)(struct gendisk*, u64 lba, const void *, u32 cnt);
};

struct block_device {
    u32 first_sector;
    u32 num_sectors;
    struct gendisk *disk;
    char name[32];
    struct block_device *next;
};

struct gpt_part_entry;

int __must_check add_gendisk(struct gendisk *disk);
int scan_partitions(struct gendisk *disk);
int __must_check create_block_dev(struct gendisk *disk,
    struct gpt_part_entry *part_entry);

#endif
