#include <stdio.h>
#include <stdint.h>

#include <kernel/gdt.h>

#define GDT_SIZE 3

typedef struct GDTEntry {
    uint32_t base;
    uint32_t limit;
    uint8_t access_byte;
    uint8_t flags;
} __attribute__((packed)) GDTEntry;

typedef struct GDT {
    uint16_t size;
    uint32_t offset;
} __attribute__((packed)) GDT;

void gdt_entry(uint8_t *target, struct GDTEntry source);
extern void gdt_set(GDT *gdt_ptr);
extern void reload_segments();

GDT gdtptr;
uint32_t segment_desc[GDT_SIZE*2];
GDTEntry gdt_entries[GDT_SIZE] = {
    {0, 0, 0, 0},
    {0, 0xFFFFF, 0x9A, 0xC},
    {0, 0xFFFFF, 0x92, 0xC}
    //{&TSS, sizeof(TSS), 0x89, 0x0}
};

void gdt_initialize() {
    gdtptr.size = (64 * GDT_SIZE) - 1;
    gdtptr.offset = (uint32_t)&segment_desc;

    uint8_t *ptr = (uint8_t*)segment_desc;
    gdt_entry(ptr, gdt_entries[0]);
    gdt_entry(ptr+8, gdt_entries[1]);
    gdt_entry(ptr+16, gdt_entries[2]);

    gdt_set(&gdtptr);	
    reload_segments();
    
    printf("GDT initialized\n");
}

void gdt_entry(uint8_t *target, struct GDTEntry source)
{
    // Check the limit to make sure that it can be encoded
    if (source.limit > 0xFFFFF) {printf("GDT cannot encode limits larger than 0xFFFFF");}

    // Encode the limit
    target[0] = source.limit & 0xFF;
    target[1] = (source.limit >> 8) & 0xFF;
    target[6] = (source.limit >> 16) & 0x0F;

    // Encode the base
    target[2] = source.base & 0xFF;
    target[3] = (source.base >> 8) & 0xFF;
    target[4] = (source.base >> 16) & 0xFF;
    target[7] = (source.base >> 24) & 0xFF;

    // Encode the access byte
    target[5] = source.access_byte;

    // Encode the flags
    target[6] |= (source.flags << 4);
}
