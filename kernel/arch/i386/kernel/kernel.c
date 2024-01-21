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

	kvirtual_free(virtual_alloc, 4);
	printf("Freed %x\n", virtual_alloc);

	while (1) {
		asm("hlt");
	}
}
