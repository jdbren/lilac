#include <stdio.h>

#include <kernel/panic.h>

void div0_interrupt(void) 
{
    kerror("Divide by zero int\n");
}

void pg_fault_handler(int error_code) 
{
    int addr = 0;
	asm volatile ("mov %%cr2, %0 \n\t" : "=r"(addr));

	printf("Fault address: %x\n", addr);
    printf("Error code: %x\n", error_code);
    kerror("Page fault detected\n");
}

void gp_fault_handler(int error_code) 
{
    printf("Error code: %x\n", error_code);
    kerror("General protection fault detected\n");
}

void invalid_opcode_handler(void) 
{
    kerror("Invalid opcode detected\n");
}
