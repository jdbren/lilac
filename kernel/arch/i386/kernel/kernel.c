#include <string.h>
#include <utility/multiboot.h>
#include <kernel/tty.h>
#include <kernel/panic.h>
#include <kernel/keyboard.h>
#include <kernel/elf.h>
#include <gdt.h>
#include <idt.h>
#include <fs_init.h>
#include <pgframe.h>
#include <paging.h>
#include <mm/kmm.h>
#include <mm/kheap.h>
#include <fs/mbr.h>
#include <fs/fat32.h>

void jump_usermode(u32 addr);

void kernel_main(multiboot_info_t* mbd, unsigned int magic)
{
	terminal_initialize();
	gdt_initialize();

	if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
		kerror("Invalid magic number\n");

	idt_initialize();
	keyboard_initialize();
	enable_interrupts();
	mm_init();
	fs_init(mbd);
	void *ptr = fat32_read_file("/bin/code");
	void *jmp = elf32_load(ptr);

	jump_usermode((u32)jmp);

	while (1) {
		asm("hlt");
	}
}


