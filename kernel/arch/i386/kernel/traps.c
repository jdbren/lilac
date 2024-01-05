#include <stdio.h>

#include <kernel/panic.h>

void div0_interrupt() {
    kerror("Divide by zero int\n");
}

void pg_fault_handler() {
    int i;
	asm("mov %%cr2, %0" : "=r"(i));
	printf("Fault address: %x\n", i);
    kerror("Page fault detected\n");
}


