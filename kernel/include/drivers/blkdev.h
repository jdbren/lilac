#ifndef _BLKDEV_H
#define _BLKDEV_H

#include <lilac/types.h>
#include <lilac/config.h>
#include <lilac/sync.h>
#include <fs/types.h>

struct gendisk {
    int major;
    int first_minor;
    char driver[8];
    const struct disk_operations *ops;
    struct block_device *partitions;
    u32 num_partitions;
    u32 sector_size;
    u64 sector_count;
    // struct request_queue *queue;
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
    u32 first_sector_lba;
    u32 num_sectors;
    struct gendisk *disk;
    enum fs_type type;
    dev_t devnum;
    char name[32];
    struct inode *bd_inode;
    struct block_device *next;
    struct mutex bd_holder_lock;
};

struct blkio_buffer {
    struct block_device *bdev;
    u32 lba;
    u32 sector_cnt;
    void *buffer;
    struct list_head b_list;
};

struct gpt_part_entry;

__must_check
int add_gendisk(struct gendisk *disk);
int scan_partitions(struct gendisk *disk);
struct block_device *get_bdev(int major);

#endif
