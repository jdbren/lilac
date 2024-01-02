#include <stdio.h>

#include <kernel/tty.h>
#include <kernel/gdt.h>
#include <kernel/keyboard.h>
#include <kernel/idt.h>

void kernel_main(void) {
	terminal_initialize();
	
	gdt_initialize();

	/*
	int i;
	asm("mov %%cs, %0" : "=r"(i));
	printf("CS: %x\n", i);
	*/

	idt_initialize();
	keyboard_initialize();
	
	enable_interrupts();

	while(1) {
		asm("nop");
	}
}
