#ifndef __GDT_H
#define __GDT_H

#include <lilac/types.h>
#include <asm/gdt.h>

#define GDT_ENTRY(base, limit, access, flags) \
( \
    (limit & 0xff)        << 8 * 0 | \
    (limit >> 8 & 0xff)   << 8 * 1 | \
    (limit >> 16 & 0x0f)  << 8 * 6 | \
    (base & 0xff)         << 8 * 2 | \
    (base >> 8 & 0xff)    << 8 * 3 | \
    (base >> 16 & 0xff)   << 8 * 4 | \
    (base >> 24 & 0xff)   << 8 * 7 | \
    (access)              << 8 * 5 | \
    (flags << 4)          << 8 * 6   \
)

#ifdef ARCH_x86_64
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
};
#else
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
};
#endif

void gdt_init(void);
void set_tss_esp0(uintptr_t esp0);
struct tss* get_tss(void);

#endif
