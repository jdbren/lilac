#include <string.h>
#include <kernel/tty.h>
#include <kernel/panic.h>
#include <kernel/keyboard.h>
#include <gdt.h>
#include <idt.h>
#include <fs_init.h>
#include <utility/multiboot.h>
#include <pgframe.h>
#include <paging.h>
#include <mm/kmm.h>
#include <mm/kheap.h>
#include <fs/mbr.h>
#include <fs/fat32.h>
#include <kernel/elf.h>

void jump_usermode(u32 addr);

void kernel_main(multiboot_info_t* mbd, unsigned int magic)
{
	terminal_initialize();
	gdt_initialize();

	if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
		kerror("Invalid magic number\n");

	idt_initialize();
	keyboard_initialize();
	mm_init();
	fs_init(mbd);
	void *ptr;
	ptr = fat32_read_file("/bin/code", ptr);
	ptr = elf32_load(ptr);
	printf("ELF entry: %x\n", (u32)ptr);
	void *phys = alloc_frame();
	void *vaddr = (void*)0x8048000;
	map_page(phys, vaddr, 0x7);
	memcpy(vaddr, ptr, 64);
	jump_usermode((u32)ptr);

	while (1) {
		asm("hlt");
	}
}


