#ifndef _BLKDEV_H
#define _BLKDEV_H

#include <lilac/types.h>
#include <lilac/config.h>
#include <lilac/sync.h>
#include <fs/fs_type.h>

#define SECTOR_SHIFT 9
#define SECTOR_SIZE 512

struct super_block;

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

struct gpt_guid {
    uint8_t bytes[16];
};

typedef enum {
    GPT_UNKNOWN = -1,
    GPT_UNUSED,
    GPT_MBR_SCHEME,
    GPT_EFI_SYSTEM,
    GPT_BIOS_BOOT,
    GPT_MICROSOFT_BASIC_DATA,
    GPT_LINUX_FILESYSTEM_DATA,
} gpt_part_type;

struct disk_operations {
    int (*disk_read)(struct gendisk*, u64 lba, void *, u32 cnt);
    int (*disk_write)(struct gendisk*, u64 lba, const void *, u32 cnt);
};

struct block_device {
    u64 first_sector_lba;
    u32 num_sectors;
    u32 blocksize_bits;
    struct gendisk *disk;
    dev_t devnum;
    uint8_t uuid[16];
    gpt_part_type partition_type;
    char name[32];
    // struct inode *bd_inode;
    struct block_device *next;
    struct mutex bd_holder_lock;
};

struct blkio_desc {
    struct {
        int valid   :1;
        int dirty   :1;
        int locked  :1;
    } b_state;
    u64 b_block;
    size_t b_size;
    struct block_device *b_bdev;
    unsigned char *b_data;
    struct page *b_page;
    struct list_head b_list;
};

__must_check
int add_gendisk(struct gendisk *disk);
int scan_partitions(struct gendisk *disk);
struct block_device *get_bdev(int major);
struct block_device *get_bdev_by_uuid(const char *uuid);

struct blkio_desc * bread(struct block_device *bdev, u64 block_num, size_t size);
int brelease(struct blkio_desc *buf);
void bdrop(struct blkio_desc *bio);

int set_blocksize(struct block_device *bdev, int size);
int sb_set_blocksize(struct super_block *sb, int size);
int sb_min_blocksize(struct super_block *sb, int size);

#endif
