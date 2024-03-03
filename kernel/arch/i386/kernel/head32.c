// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <string.h>
#include <kernel/lilac.h>
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


struct multiboot_info mbd;
static struct acpi_info acpi;

static void parse_multiboot(u32, struct multiboot_info*);

void kernel_early(unsigned int multiboot)
{
	terminal_init();
	gdt_init();
	idt_init();
	parse_multiboot(multiboot, &mbd);

	mm_init(mbd.mmap, mbd.meminfo->mem_upper);
	parse_acpi((void*)mbd.new_acpi->rsdp, &acpi);

	asm ("hlt");

	fs_init(mbd.boot_dev);
	apic_init(acpi.madt);
	keyboard_init();
	timer_init();
	enable_interrupts();

	//ap_init(acpi.madt->core_cnt);

	start_kernel();
}

static void parse_multiboot(u32 addr, struct multiboot_info *mbd)
{
	if (addr & 7)
		kerror("Unaligned mbi:\n");

	struct multiboot_tag *tag;
	for (tag = (struct multiboot_tag *) (addr + 8);
		tag->type != MULTIBOOT_TAG_TYPE_END;
		tag = (struct multiboot_tag *) ((multiboot_uint8_t *) tag
			+ ((tag->size + 7) & ~7)))
	{
		printf("Tag: %d\n", tag->type);
		switch (tag->type) {
			case MULTIBOOT_TAG_TYPE_CMDLINE:
				mbd->cmdline = ((struct multiboot_tag_string*)tag)->string;
			break;
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
			case MULTIBOOT_TAG_TYPE_EFI32:
				mbd->efi32 = (struct multiboot_tag_efi32*)tag;
			break;
			// case MULTIBOOT_TAG_TYPE_ACPI_OLD:
			// 	mbd->old_acpi = (struct multiboot_tag_old_acpi*)tag;
			// break;
			case MULTIBOOT_TAG_TYPE_ACPI_NEW:
				mbd->new_acpi = (struct multiboot_tag_new_acpi*)tag;
			break;
			case MULTIBOOT_TAG_TYPE_EFI_MMAP:
				mbd->efi_mmap = (struct multiboot_tag_efi_mmap*)tag;
			break;
			case MULTIBOOT_TAG_TYPE_LOAD_BASE_ADDR:
				mbd->base_addr = (struct multiboot_tag_load_base_addr*)tag;
			break;
			default:
				printf("Unknown tag: %d\n", tag->type);
			break;
		}
	}
}

void *get_rsdp()
{
	return (void*)mbd.new_acpi->rsdp;
}
