#include <timer.h>
#include <apic.h>
#include <idt.h>
#include <io.h>

extern void timer_handler(void);
extern void init_PIT(int freq);

volatile u32 timer_cnt;

void timer_init(void)
{
	init_PIT(1000);
    idt_entry(0x20, (u32)timer_handler, 0x08, INT_GATE);
    io_apic_entry(0, 0x20, 0, 0);
	printf("Timer initialized\n");
}

void sleep(u32 millis)
{
    timer_cnt = millis;
    while (timer_cnt > 0) {
        asm("hlt");
    }
}

unsigned read_pit_count(void) {
	unsigned count = 0;
 
	// Disable interrupts
	asm("cli");
 
	// al = channel in bits 6 and 7, remaining bits clear
	outb(0x43,0b0000000);
 
	count = inb(0x40);		// Low byte
	count |= inb(0x40)<<8;		// High byte
 
	return count;
}

void set_pit_count(unsigned count) {
	// Disable interrupts
	asm("cli");
 
	// Set low byte
	outb(0x40,count&0xFF);		// Low byte
	outb(0x40,(count&0xFF00)>>8);	// High byte
}

void timer_print(void) {
	printf("Timer int\n");
}
