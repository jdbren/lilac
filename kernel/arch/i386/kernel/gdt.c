#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <arch/x86/gdt.h>
#include <kernel/panic.h>

#define GDT_SIZE 6

struct gdt_entry {
    uint32_t base;
    uint32_t limit;
    uint8_t access_byte;
    uint8_t flags;
} __attribute__((packed));

struct GDT {
    uint16_t size;
    uint32_t offset;
} __attribute__((packed));

struct tss_entry {
	uint32_t prev_tss; // The previous TSS - with hardware task switching these form a kind of backward linked list.
	uint32_t esp0;     // The stack pointer to load when changing to kernel mode.
	uint32_t ss0;      // The stack segment to load when changing to kernel mode.
	// Everything below here is unused.
	uint32_t esp1; // esp and ss 1 and 2 would be used when switching to rings 1 or 2.
	uint32_t ss1;
	uint32_t esp2;
	uint32_t ss2;
	uint32_t cr3;
	uint32_t eip;
	uint32_t eflags;
	uint32_t eax;
	uint32_t ecx;
	uint32_t edx;
	uint32_t ebx;
	uint32_t esp;
	uint32_t ebp;
	uint32_t esi;
	uint32_t edi;
	uint32_t es;
	uint32_t cs;
	uint32_t ss;
	uint32_t ds;
	uint32_t fs;
	uint32_t gs;
	uint32_t ldtr;
	uint16_t trap;
	uint16_t iomap_base;
    uint32_t ssp;
} __attribute__((packed));


struct tss_entry tss;
struct GDT gdt;
uint32_t segment_desc[GDT_SIZE*2];

struct gdt_entry gdt_entries[GDT_SIZE] = {
    {0, 0, 0, 0},
    {0, 0xFFFFF, 0x9A, 0xC},
    {0, 0xFFFFF, 0x92, 0xC},
    {0, 0xFFFFF, 0xFA, 0xC},
    {0, 0xFFFFF, 0xF2, 0xC},
    {(uint32_t)&tss, sizeof(tss), 0x89, 0x0}
};

void gdt_entry(uint8_t *target, struct gdt_entry *source);
void gdt_set(struct GDT *gdt_ptr);
void reload_segments(void);
void flush_tss(void);

void gdt_initialize(void)
{
    gdt.size = (64 * GDT_SIZE) - 1;
    gdt.offset = (uint32_t)&segment_desc;

    for (int i = 0; i < GDT_SIZE; i++)
        gdt_entry((uint8_t*)segment_desc + i*8, &gdt_entries[i]);
    
    // Ensure the TSS is initially zero'd.
	memset(&tss, 0, sizeof(tss));
 
	tss.ss0 = 0x10;                         // Set the kernel stack segment.
	asm("\t movl %%esp,%0" : "=r"(tss.esp0)); // Set the kernel stack pointer.
	//note that CS is loaded from the IDT entry and should be the regular kernel code segment

    gdt_set(&gdt);	
    reload_segments();
    flush_tss();

    printf("GDT initialized\n");
}

void gdt_entry(uint8_t *target, struct gdt_entry *source)
{
    // Check the limit to make sure that it can be encoded
    if (source->limit > 0xFFFFF) {
        kerror("GDT cannot encode limits larger than 0xFFFFF");
    }

    // Encode the limit
    target[0] = source->limit & 0xFF;
    target[1] = (source->limit >> 8) & 0xFF;
    target[6] = (source->limit >> 16) & 0x0F;

    // Encode the base
    target[2] = source->base & 0xFF;
    target[3] = (source->base >> 8) & 0xFF;
    target[4] = (source->base >> 16) & 0xFF;
    target[7] = (source->base >> 24) & 0xFF;

    // Encode the access byte
    target[5] = source->access_byte;

    // Encode the flags
    target[6] |= (source->flags << 4);
}

void set_kernel_stack(uint32_t stack) { // Used when an interrupt occurs
	tss.esp0 = stack;
}
