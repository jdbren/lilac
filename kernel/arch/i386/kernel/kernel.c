#include <stdio.h>
#include <string.h>

#include <kernel/tty.h>
#include <kernel/panic.h>
#include <kernel/keyboard.h>
#include <arch/x86/gdt.h>
#include <arch/x86/idt.h>
#include <arch/x86/multiboot.h>
#include <mm/pgframe.h>
#include <mm/paging.h>
#include <mm/kmm.h>
#include <mm/kheap.h>
#include <fs/mbr.h>
#include <fs/fat32.h>
#include <arch/x86/asm/diskio.h>

#define BOOT_LOCATION 0x7C00

void jump_usermode(void);

void kernel_main(multiboot_info_t* mbd, unsigned int magic)
{
	terminal_initialize();
	gdt_initialize();
	int boot_device = mbd->boot_device >> 24;
	int boot_partition = (mbd->boot_device >> 16) & 0xFF;
	printf("Boot device: %x\n", boot_device);
	printf("Boot partition: %x\n", boot_partition);

	MBR_t *mbr = (MBR_t*)BOOT_LOCATION;
	printf("MBR signature: %x\n", mbr->signature);

	struct PartitionEntry *partition = &mbr->partition_table[boot_partition];

	uint32_t fat32_lba;
	if(partition->type == 0xB) {
		fat32_lba = partition->lba_first_sector;
		printf("fat32_lba: %x\n", fat32_lba);
	}
	else {
		kerror("Boot partition is not FAT32\n");
	}

	disk_read(fat32_lba, 1, 0x7c00);

	fat_BS_t *ptr = (void*)0x7c00;
	print_fat32_data(ptr);
	
	asm volatile("hlt");
    /* Loop through the memory map and display the values */

	idt_initialize();
	keyboard_initialize();
	enable_interrupts();
	mm_init();
	// jump_usermode();

	while (1) {
		asm("hlt");
	}
}


