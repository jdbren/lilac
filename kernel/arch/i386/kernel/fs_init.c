#include <kernel/types.h>
#include <kernel/panic.h>
#include <fs/mbr.h>
#include <fs/fat32.h>
#include <utility/multiboot2.h>
#include <fs_init.h>

#define BOOT_LOCATION 0x7C00

void mbr_read(int boot_partition);

void fs_init(struct multiboot_tag_bootdev* boot_dev)
{
    int boot_device = boot_dev->biosdev;
    int boot_partition = (boot_dev->slice) & 0xFF;

    if (boot_device == 0x80)
        mbr_read(boot_partition);
    else
        kerror("Boot device is not the first hard disk\n");
}

void mbr_read(int boot_partition)
{
	MBR_t const *mbr = (MBR_t const*)BOOT_LOCATION;
    if (mbr->signature != 0xAA55)
        kerror("Invalid MBR signature\n");
 
    struct PartitionEntry *partition = &mbr->partition_table[boot_partition];

	u32 fat32_lba;
	if (partition->type == 0xB) {
		fat32_lba = partition->lba_first_sector;
		fat32_init(boot_partition, fat32_lba);
	}
	else {
		kerror("Boot partition is not FAT32\n");
	}
}
