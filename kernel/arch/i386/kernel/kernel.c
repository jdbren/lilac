#include <stdio.h>
#include <string.h>

#include <arch/x86/multiboot.h>
#include <kernel/tty.h>
#include <kernel/panic.h>
#include <kernel/keyboard.h>
#include <arch/x86/gdt.h>
#include <arch/x86/idt.h>
#include <mm/pgframe.h>
#include <mm/paging.h>
#include <mm/kmm.h>
#include <mm/kheap.h>

void jump_usermode(void);

void kernel_main(multiboot_info_t* mbd, unsigned int magic)
{
	terminal_initialize();
	gdt_initialize();
	if(magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        kerror("invalid magic number!");
    }
	printf("Multiboot info: %x\n", mbd);
	printf("Multiboot magic: %x\n", magic);
	/* Check bit 6 to see if we have a valid memory map */
    if(!(mbd->flags >> 6 & 0x1)) {
        kerror("invalid memory map given by GRUB bootloader");
    }
 
    /* Loop through the memory map and display the values */
    int i;
    for(i = 0; i < mbd->mmap_length; 
        i += sizeof(multiboot_memory_map_t)) 
    {
        multiboot_memory_map_t* mmmt = 
            (multiboot_memory_map_t*) (mbd->mmap_addr + i);
 
        printf("Start Addr: %x | Length: %x | Size: %x | Type: %d\n",
            mmmt->addr_high + mmmt->addr_low, mmmt->len_high + mmmt->len_low, mmmt->size, mmmt->type);
 
        if(mmmt->type == MULTIBOOT_MEMORY_AVAILABLE) {
            /* 
             * Do something with this memory block!
             * BE WARNED that some of memory shown as availiable is actually 
             * actively being used by the kernel! You'll need to take that
             * into account before writing to memory!
             */
        }
    }

	idt_initialize();
	keyboard_initialize();
	enable_interrupts();
	mm_init();
	// jump_usermode();

	// printf("\nMore testing:\n");
	// void *virtual_alloc = kvirtual_alloc(1);
	// printf("Allocated %x\n", virtual_alloc);
	// memset(virtual_alloc, 'a', 4096);
	// printf("%x: %c\n\n", (char*)virtual_alloc + 10, *((char*)virtual_alloc + 10));

	// virtual_alloc = kvirtual_alloc(2);
	// printf("Allocated %x\n", virtual_alloc);
	// memset(virtual_alloc, 'b', 8128);
	// printf("%x: %c\n\n", (char*)virtual_alloc + 5000, *((char*)virtual_alloc + 5000));

	// kvirtual_free(virtual_alloc, 2);
	// printf("Freed %x\n", virtual_alloc);

	// virtual_alloc = kvirtual_alloc(1);
	// printf("Allocated %x\n", virtual_alloc);

	// virtual_alloc = kvirtual_alloc(1);
	// printf("Allocated %x\n", virtual_alloc);

	// virtual_alloc = kvirtual_alloc(7);
	// printf("Allocated %x\n", virtual_alloc);

	// kvirtual_free(virtual_alloc, 7);
	// printf("Freed %x\n", virtual_alloc);

	// virtual_alloc = kvirtual_alloc(4);
	// printf("Allocated %x\n", virtual_alloc);

	// virtual_alloc = kvirtual_alloc(20);
	// printf("Allocated %x\n", virtual_alloc);

	// virtual_alloc = kvirtual_alloc(1);
	// printf("Allocated %x\n", virtual_alloc);

	// int *alloc = (int*)kmalloc(sizeof(int) * 101);
	// printf("Allocated %x\n", alloc);
	// alloc[45] = 0x12345678;
	// printf("%x: %x\n", &alloc[45], alloc[45]);
	// kfree(alloc);

	// alloc = (int*)kmalloc(sizeof(int) * 4723);
	// printf("allocated %x\n", alloc);
	// alloc[504] = 0x87654321;
	// printf("%x: %x\n", &alloc[504], alloc[504]);
	// kfree(alloc);

	// alloc = (int*)kmalloc(sizeof(int) * 10843);
	// printf("allocated %x\n", alloc);
	// alloc[6823] = 0xAABBCCDD;
	// printf("%x: %x\n", &alloc[6823], alloc[6823]);
	// kfree(alloc);

	while (1) {
		asm("hlt");
	}
}


