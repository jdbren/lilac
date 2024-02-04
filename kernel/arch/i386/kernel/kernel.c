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

struct multiboot_tag *tag;
static struct multiboot_info {
	u32 mem_lower;
	u32 mem_upper;
	struct {
		u32 biosdev;
		u32 slice;
		u32 part;
	} boot_dev;
	u32 rsdp;
} mbd;

void jump_usermode(u32 addr);

void kernel_main(u32 addr)
{
	terminal_initialize();
	gdt_initialize();
	idt_initialize();
	keyboard_initialize();
	enable_interrupts();
	mm_init();

	if (addr & 7) {
		kerror("Unaligned mbi:\n");
		return;
	}

	tag = (struct multiboot_tag *)addr;

	printf("addr: %x\n", tag);
	printf("Size: %x\n", tag->type);

	int *i = kmalloc(200);
	*i = 5;
	printf("i: %d\n", *i);
	map_page((void*)0xBFFED000, (void*)0xBFFED000, 0x3);
	*(int*)0xBFFED000 = 5;
	printf("i: %d\n", *(int*)0xBFFED000);

	for (tag = (struct multiboot_tag *) (addr + 8);
		tag->type != MULTIBOOT_TAG_TYPE_END;
		tag = (struct multiboot_tag *) ((multiboot_uint8_t *) tag 
										+ ((tag->size + 7) & ~7)))
	{
		printf ("Tag %x, Size %x\n", tag->type, tag->size);
		switch (tag->type) {
			case MULTIBOOT_TAG_TYPE_CMDLINE:
				printf ("Command line = %s\n",
					((struct multiboot_tag_string *) tag)->string);
			break;
			case MULTIBOOT_TAG_TYPE_BOOT_LOADER_NAME:
				printf ("Boot loader name = %s\n",
					((struct multiboot_tag_string *) tag)->string);
			break;
			case MULTIBOOT_TAG_TYPE_MODULE:
				printf ("Module at %x-%x. Command line %s\n",
					((struct multiboot_tag_module *) tag)->mod_start,
					((struct multiboot_tag_module *) tag)->mod_end,
					((struct multiboot_tag_module *) tag)->cmdline);
			break;
			case MULTIBOOT_TAG_TYPE_BASIC_MEMINFO:
				printf ("mem_lower = %dKB, mem_upper = %dKB\n",
					((struct multiboot_tag_basic_meminfo *) tag)->mem_lower,
					((struct multiboot_tag_basic_meminfo *) tag)->mem_upper);
			break;
			case MULTIBOOT_TAG_TYPE_BOOTDEV:
				printf ("Boot device %x,%d,%d\n",
					((struct multiboot_tag_bootdev *) tag)->biosdev,
					((struct multiboot_tag_bootdev *) tag)->slice,
					((struct multiboot_tag_bootdev *) tag)->part);
			break;
			case MULTIBOOT_TAG_TYPE_MMAP: {
				multiboot_memory_map_t *mmap;

				printf ("mmap\n");
		
				for (mmap = ((struct multiboot_tag_mmap *) tag)->entries;
					(multiboot_uint8_t *) mmap 
					< (multiboot_uint8_t *) tag + tag->size;
					mmap = (multiboot_memory_map_t *) 
					((unsigned long) mmap
						+ ((struct multiboot_tag_mmap *) tag)->entry_size))
				printf (" base_addr = %x%x,"
						" length = %x%x, type = %x\n",
						(unsigned) (mmap->addr >> 32),
						(unsigned) (mmap->addr & 0xffffffff),
						(unsigned) (mmap->len >> 32),
						(unsigned) (mmap->len & 0xffffffff),
						(unsigned) mmap->type);
			}
			break;
			case MULTIBOOT_TAG_TYPE_ACPI_OLD:
				printf("ACPI old\n");
				printf("Type: %x, size %x, rsdp %x\n", 
					((struct multiboot_tag_old_acpi *)tag)->type, 
					((struct multiboot_tag_old_acpi *)tag)->size,
					((struct multiboot_tag_old_acpi *)tag)->rsdp);
				struct RSDP *rsdp = (struct RSDP *)(((struct multiboot_tag_old_acpi *)tag)->rsdp);
				printf("Signature: %s\n", rsdp->Signature);
				printf("Checksum: %x\n", rsdp->Checksum);
				printf("OEMID: %s\n", rsdp->OEMID);
				printf("Revision: %x\n", rsdp->Revision);
				printf("RsdtAddress: %x\n", rsdp->RsdtAddress);
				u8 check;
				for (int i = 0; i < sizeof(*rsdp); i++) {
					check += ((char *)rsdp)[i];
				}
				check -= rsdp->Checksum;
				printf("Checksum: %x\n", check);
				if((u8)(check + rsdp->Checksum) == 0) {
					printf("Checksum is correct\n");
				} else {
					printf("Checksum is incorrect\n");
				}

				map_page((void*)(rsdp->RsdtAddress & 0xfffff000), (void*)(rsdp->RsdtAddress & 0xfffff000), 0x1);
				struct SDTHeader *rsdt = (struct SDTHeader *)rsdp->RsdtAddress;
				printf("Signature: %s\n", rsdt->Signature);
				

			break;
		}
	}




	// fs_init(mbd);
	// void *ptr = fat32_read_file("/bin/code");
	// void *jmp = elf32_load(ptr);

	// jump_usermode((u32)jmp);

	while (1) {
		asm("hlt");
	}
}


