#include <stdio.h>
#include <string.h>

#include <kernel/tty.h>
#include <kernel/keyboard.h>
#include <arch/x86/gdt.h>
#include <arch/x86/idt.h>
#include <mm/pgframe.h>
#include <mm/paging.h>
#include <mm/kmm.h>

void kernel_main(void) {
	terminal_initialize();
	gdt_initialize();
	idt_initialize();
	keyboard_initialize();
	enable_interrupts();
	mm_init();

	/*
	void *physical_alloc = alloc_frames(3);
	int *virt_addr = (int*)0x55555000;
	map_pages(physical_alloc, (void*)virt_addr, 0x3, 3);
	virt_addr[200] = 0x12345678;
	printf("%x: %x\n", virt_addr + 200, virt_addr[200]);
	unmap_pages(virt_addr, 3);
	free_frames(physical_alloc, 3);
	alloc_frame();
	alloc_frame();
	alloc_frame();
	*/

	printf("\nMore testing:\n");
	void *virtual_alloc = kvirtual_alloc(1);
	printf("Allocated %x\n", virtual_alloc);
	memset(virtual_alloc, 'a', 4096);
	printf("%x: %c\n", (char*)virtual_alloc + 10, *((char*)virtual_alloc + 10));

	virtual_alloc = kvirtual_alloc(2);
	printf("Allocated %x\n", virtual_alloc);
	memset(virtual_alloc, 'b', 8128);
	printf("%x: %c\n", (char*)virtual_alloc + 5000, *((char*)virtual_alloc + 5000));

	while(1) {
		asm("hlt");
	}
}
