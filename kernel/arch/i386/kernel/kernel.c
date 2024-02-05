#include <string.h>
#include <utility/multiboot2.h>
#include <utility/acpi.h>
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


static struct multiboot_info {
	char *bootloader;
	struct multiboot_tag_basic_meminfo *meminfo;
	struct multiboot_tag_bootdev *boot_dev;
	struct multiboot_tag_mmap *mmap;
	struct multiboot_tag_old_acpi *acpi;
} mbd;

void jump_usermode(u32 addr);
void parse_multiboot(u32, struct multiboot_info*);

void kernel_main(unsigned int addr)
{
	terminal_init();
	gdt_init();
	idt_init();
	keyboard_init();
	enable_interrupts();
	parse_multiboot(addr, &mbd);
	mm_init(mbd.mmap, mbd.meminfo->mem_upper);
	read_rsdp((void*)mbd.acpi->rsdp);
	
	// fs_init(mbd);
	// void *ptr = fat32_read_file("/bin/code");
	// void *jmp = elf32_load(ptr);
	// jump_usermode((u32)jmp);

	while (1) {
		asm("hlt");
	}
}

void parse_multiboot(u32 addr, struct multiboot_info *mbd)
{
	if (addr & 7)
		kerror("Unaligned mbi:\n");

	struct multiboot_tag *tag;
	for (tag = (struct multiboot_tag *) (addr + 8);
		tag->type != MULTIBOOT_TAG_TYPE_END;
		tag = (struct multiboot_tag *) ((multiboot_uint8_t *) tag 
										+ ((tag->size + 7) & ~7)))
	{
		switch (tag->type) {
			case MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME:
				mbd->bootloader = ((struct multiboot_tag_string*)tag)->string;
			break;
			case MULTIBOOT_TAG_TYPE_BASIC_MEMINFO:
				mbd->meminfo = (struct multiboot_tag_basic_meminfo*)tag;
			break;
			case MULTIBOOT_TAG_TYPE_BOOTDEV:
				mbd->boot_dev = (struct multiboot_tag_bootdev*)tag;
			break;
			case MULTIBOOT_TAG_TYPE_MMAP: 
				mbd->mmap = (struct multiboot_tag_mmap*)tag;
			break;
			case MULTIBOOT_TAG_TYPE_ACPI_OLD:
				mbd->acpi = (struct multiboot_tag_old_acpi*)tag;
			break;
		}
	}
}
