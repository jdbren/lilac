#include <string.h>
#include <utility/multiboot2.h>
#include <kernel/kmain.h>
#include <kernel/tty.h>
#include <kernel/panic.h>
#include <kernel/keyboard.h>
#include <kernel/elf.h>
#include <kernel/process.h>
#include <acpi/acpi.h>
#include <mm/kmm.h>
#include <mm/kheap.h>
#include <fs/vfs.h>
#include <fs/fat32.h>
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


void arch_context_switch(struct task *prev, struct task *next)
{
	printf("Switching from %s to %s\n", prev->name, next->name);
    asm volatile (
        "pushfl\n\t"
        "pushl %%ebp\n\t"
        "movl %%esp, %[prev_sp]\n\t"
        "movl %[next_pg], %%eax\n\t"
        "movl %%eax, %%cr3\n\t"
        "movl %[next_sp], %%esp\n\t"
        "movl $1f, %[prev_ip]\n\t"
        "pushl %[next_ip]\n\t"
        "ret\n"
        "1:\t"
        "popl %%ebp\n\t"
        "popfl\n\t"
        : [prev_sp] "=m" (prev->stack),
          [prev_ip] "=m" (prev->pc)
        : [next_sp] "m" (next->stack),
          [next_ip] "m" (next->pc),
          [next_pg] "m" (next->pgd)
        :  "eax", "memory"
    );
}
