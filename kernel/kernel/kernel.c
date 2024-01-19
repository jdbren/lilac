#include <stdio.h>
#include <string.h>

#include <kernel/tty.h>
#include <kernel/gdt.h>
#include <kernel/keyboard.h>
#include <kernel/idt.h>
#include <kernel/pgframe.h>
#include <kernel/paging.h>
#include <kernel/mm.h>

void kernel_main(void) {
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
	printf("%x: %c\n", (char*)virtual_alloc + 10, *((char*)virtual_alloc + 10));

	virtual_alloc = kvirtual_alloc(2);
	memset(virtual_alloc, 'b', 8128);
	printf("%x: %c\n", (char*)virtual_alloc + 10000, *((char*)virtual_alloc + 10000));


	while(1) {
		asm("hlt");
	}
}
