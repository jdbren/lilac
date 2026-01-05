// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef __ASM_GDT_H
#define __ASM_GDT_H

#define SEG_DESCTYPE(x)  ((x) << 0x04) // Descriptor type (0 for system, 1 for code/data)
#define SEG_PRES(x)      ((x) << 0x07) // Present
#define SEG_SAVL(x)      ((x) << 0x0C) // Available for system use
#define SEG_LONG(x)      ((x) << 0x0D) // Long mode
#define SEG_SIZE(x)      ((x) << 0x0E) // Size (0 for 16-bit, 1 for 32)
#define SEG_GRAN(x)      ((x) << 0x0F) // Granularity (0 for 1B - 1MB, 1 for 4KB - 4GB)
#define SEG_PRIV(x)     (((x) &  0x03) << 0x05)   // Set privilege level (0 - 3)

#define SEG_DATA_RD        0x00 // Read-Only
#define SEG_DATA_RDA       0x01 // Read-Only, accessed
#define SEG_DATA_RDWR      0x02 // Read/Write
#define SEG_DATA_RDWRA     0x03 // Read/Write, accessed
#define SEG_DATA_RDEXPD    0x04 // Read-Only, expand-down
#define SEG_DATA_RDEXPDA   0x05 // Read-Only, expand-down, accessed
#define SEG_DATA_RDWREXPD  0x06 // Read/Write, expand-down
#define SEG_DATA_RDWREXPDA 0x07 // Read/Write, expand-down, accessed
#define SEG_CODE_EX        0x08 // Execute-Only
#define SEG_CODE_EXA       0x09 // Execute-Only, accessed
#define SEG_CODE_EXRD      0x0A // Execute/Read
#define SEG_CODE_EXRDA     0x0B // Execute/Read, accessed
#define SEG_CODE_EXC       0x0C // Execute-Only, conforming
#define SEG_CODE_EXCA      0x0D // Execute-Only, conforming, accessed
#define SEG_CODE_EXRDC     0x0E // Execute/Read, conforming
#define SEG_CODE_EXRDCA    0x0F // Execute/Read, conforming, accessed

#ifdef __x86_64__
#define TSS_rsp0 4
#define TSS_rsp1 12
#define TSS_rsp2 20
#define TSS_ist1 28
#define TSS_ist2 36
#define TSS_ist3 44
#define TSS_ist4 52
#define TSS_ist5 60
#define TSS_ist6 68
#define TSS_ist7 76
#endif // __x86_64__


#ifndef __ASSEMBLY__

#include <lilac/types.h>
#include <asm/gdt.h>

#ifdef __x86_64__
struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  gran;
    u8  base_high;
    u32 base_upper;
    u32 reserved;
} __packed;

struct tss {
    u32 reserved0;
    u64 rsp0;
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist1;
    u64 ist2;
    u64 ist3;
    u64 ist4;
    u64 ist5;
    u64 ist6;
    u64 ist7;
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} __packed;
#else
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_high;
} __packed;

struct tss {
    u32 prev_tss; // unused
    u32 esp0;
    u32 ss0;
/* unused below here */
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
    u32 ldt;
    u16 trap;
    u16 iomap_base;
} __packed;
#endif

struct gdt_ptr {
    u16 limit;
    struct gdt_entry *base;
} __packed;

void gdt_init(void);
void tss_init(int cpu_id);
void set_tss_esp0(uintptr_t esp0);
struct tss* get_tss(int cpu_id);

static inline void store_gdt(volatile struct gdt_ptr *gdtp)
{
    asm volatile ("sgdt (%0)" : : "r"(gdtp));
}

static inline void load_gdt(struct gdt_ptr *gdtp)
{
    asm volatile ("lgdt (%0)" : : "r"(gdtp));
}

static inline void load_tr(u16 tss_selector)
{
    asm volatile ("ltr %0" : : "r"(tss_selector));
}

#endif /* __ASSEMBLY__ */

#endif
