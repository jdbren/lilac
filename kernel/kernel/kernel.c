#include <stdio.h>

#include <kernel/tty.h>
#include <kernel/gdt.h>
#include <kernel/keyboard.h>
#include <kernel/idt.h>
#include <kernel/pgframe.h>
#include <kernel/paging.h>

void kernel_main(void) {
	terminal_initialize();
	
	gdt_initialize();

	idt_initialize();
	keyboard_initialize();
	
	enable_interrupts();

	uint32_t *page = alloc_frame(1);
	printf("Allocated test page in main: %x\n", page);
	map_page(page, (void*)0x4F55C000, 0x3);
	int *ptr = (int*)0x4F55C000;
	*ptr = 0xDEADBEEF;
	printf("Value at %x: %x\n", ptr, *ptr);

	while(1) {
		asm("hlt");
	}
}
