#include <stdio.h>

#include <kernel/tty.h>
#include <kernel/gdt.h>

void kernel_main(void) {
	terminal_initialize();
	printf("\n\n\n");
	
	gdt_initialize();
	int i;
	asm("mov %%cs, %0" : "=r"(i));
	printf("CS: %x\n", i);
}
