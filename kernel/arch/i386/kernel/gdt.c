// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <string.h>
#include <kernel/types.h>
#include <kernel/panic.h>
#include "gdt.h"

#define GDT_SIZE 6

struct gdt_entry {
    u32 base;
    u32 limit;
    u8 access_byte;
    u8 flags;
} __attribute__((packed));

struct GDT {
    u16 size;
    u32 offset;
} __attribute__((packed));

struct tss_entry {
	u32 prev_tss; // Used for hardware task switching.
	u32 esp0;     // Kernel stack pointer.
	u32 ss0;      // Kernel stack segment.
	// All else unused
	u32 esp1;
	u32 ss1;
	u32 esp2;
	u32 ss2;
	u32 cr3;
	u32 eip;
	u32 eflags;
	u32 eax;
	u32 ecx;
	u32 edx;
	u32 ebx;
	u32 esp;
	u32 ebp;
	u32 esi;
	u32 edi;
	u32 es;
	u32 cs;
	u32 ss;
	u32 ds;
	u32 fs;
	u32 gs;
	u32 ldtr;
	u16 trap;
	u16 iomap_base;
    u32 ssp;
} __attribute__((packed));

static u32 segment_desc[GDT_SIZE*2];
static struct GDT gdt = {(64 * GDT_SIZE) - 1, (u32)&segment_desc};
struct tss_entry tss;

static struct gdt_entry gdt_entries[GDT_SIZE] = {
    {0, 0, 0, 0},
    {0, 0xFFFFF, 0x9A, 0xC},
    {0, 0xFFFFF, 0x92, 0xC},
    {0, 0xFFFFF, 0xFA, 0xC},
    {0, 0xFFFFF, 0xF2, 0xC},
    {(u32)&tss, sizeof(tss), 0x89, 0x0}
};

static void gdt_entry(u8 *target, struct gdt_entry *source);
extern void gdt_set(struct GDT *gdt_ptr);
extern void reload_segments(void);
extern void flush_tss(void);

void gdt_init(void)
{
    for (int i = 0; i < GDT_SIZE; i++)
        gdt_entry((u8*)segment_desc + i*8, &gdt_entries[i]);

    tss.ss0 = 0x10;
	tss.esp0 = 0x0;

    gdt_set(&gdt);
    reload_segments();
    flush_tss();

    printf("GDT initialized\n");
}

static void gdt_entry(u8 *target, struct gdt_entry *source)
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

void set_tss_esp0(u32 esp)
{
	tss.esp0 = esp;
}
