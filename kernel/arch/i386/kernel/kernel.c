#include <stdio.h>
#include <string.h>

#include <kernel/tty.h>
#include <kernel/keyboard.h>
#include <arch/x86/gdt.h>
#include <arch/x86/idt.h>
#include <mm/pgframe.h>
#include <mm/paging.h>
#include <mm/kmm.h>
#include <mm/kheap.h>

void kernel_main(void)
{
	terminal_initialize();
	gdt_initialize();
	idt_initialize();
	keyboard_initialize();
	enable_interrupts();
	mm_init();

	printf("\nMore testing:\n");
	void *virtual_alloc = kvirtual_alloc(1);
	printf("Allocated %x\n", virtual_alloc);
	memset(virtual_alloc, 'a', 4096);
	printf("%x: %c\n\n", (char*)virtual_alloc + 10, *((char*)virtual_alloc + 10));

	virtual_alloc = kvirtual_alloc(2);
	printf("Allocated %x\n", virtual_alloc);
	memset(virtual_alloc, 'b', 8128);
	printf("%x: %c\n\n", (char*)virtual_alloc + 5000, *((char*)virtual_alloc + 5000));

	kvirtual_free(virtual_alloc, 2);
	printf("Freed %x\n", virtual_alloc);

	virtual_alloc = kvirtual_alloc(1);
	printf("Allocated %x\n", virtual_alloc);

	virtual_alloc = kvirtual_alloc(1);
	printf("Allocated %x\n", virtual_alloc);

	virtual_alloc = kvirtual_alloc(7);
	printf("Allocated %x\n", virtual_alloc);

	kvirtual_free(virtual_alloc, 7);
	printf("Freed %x\n", virtual_alloc);

	virtual_alloc = kvirtual_alloc(4);
	printf("Allocated %x\n", virtual_alloc);

	virtual_alloc = kvirtual_alloc(20);
	printf("Allocated %x\n", virtual_alloc);

	virtual_alloc = kvirtual_alloc(1);
	printf("Allocated %x\n", virtual_alloc);

	int *alloc = (int*)kmalloc(sizeof(int) * 101);
	printf("Allocated %x\n", alloc);
	alloc[45] = 0x12345678;
	printf("%x: %x\n", &alloc[45], alloc[45]);
	kfree(alloc);

	alloc = (int*)kmalloc(sizeof(int) * 4723);
	printf("allocated %x\n", alloc);
	alloc[504] = 0x87654321;
	printf("%x: %x\n", &alloc[504], alloc[504]);
	kfree(alloc);

	alloc = (int*)kmalloc(sizeof(int) * 10843);
	printf("allocated %x\n", alloc);
	alloc[6823] = 0xAABBCCDD;
	printf("%x: %x\n", &alloc[6823], alloc[6823]);
	kfree(alloc);

	while (1) {
		asm("hlt");
	}
}
