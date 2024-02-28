// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <string.h>
#include <utility/multiboot2.h>
#include <kernel/kmain.h>
#include <kernel/tty.h>
#include <kernel/panic.h>
#include <kernel/keyboard.h>
#include <acpi/acpi.h>
#include <mm/kmm.h>
#include <mm/kheap.h>
#include "apic.h"
#include "gdt.h"
#include "idt.h"
#include "fs_init.h"
#include "timer.h"


static struct multiboot_info mbd;
static struct acpi_info acpi;

void parse_multiboot(u32, struct multiboot_info*);

void kernel_early(unsigned int multiboot)
{
	terminal_init();
	gdt_init();
	idt_init();
	parse_multiboot(multiboot, &mbd);
	mm_init(mbd.mmap, mbd.meminfo->mem_upper);
	fs_init(mbd.boot_dev);

	parse_acpi((void*)mbd.acpi->rsdp, &acpi);
	apic_init(acpi.madt);
	keyboard_init();
	timer_init();
	enable_interrupts();

	//ap_init(acpi.madt->core_cnt);

	start_kernel();
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
