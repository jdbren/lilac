#include <kernel/panic.h>

void div0_handler(void)
{
    kerror("Divide by zero int\n");
}

void pgflt_handler(int error_code)
{
    int addr = 0;
	asm volatile("mov %%cr2, %0 \n\t" : "=r"(addr));

	printf("Fault address: %x\n", addr);
    printf("Error code: %x\n", error_code);
    kerror("Page fault detected\n");
}

void gpflt_handler(int error_code)
{
    printf("Error code: %x\n", error_code);
    kerror("General protection fault detected\n");
}

void invldop_handler(void)
{
    kerror("Invalid opcode detected\n");
}

void dblflt_handler(void)
{
    kerror("Double fault detected\n");
}
