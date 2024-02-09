#include <kernel/types.h>
#include <apic.h>
#include <paging.h>

#define IOAPIC_REGSEL     0x0
#define IOAPIC_DATA       0x10
#define IOAPIC_REDTBL(n) (0x10 + 2 * n)

#define IOAPIC_ID 0
#define IOAPIC_VER 1
#define IOAPIC_ARB 2

typedef struct redir_entry {
    u8 vector;
    u8 flags;
    u8 mask;
    u8 reserved[4];
    u8 dest;
} __attribute__((packed)) redir_entry_t;

static uintptr_t ioapic_base;


static inline void write_reg(const u8 offset, const u32 val) 
{
    /* tell IOREGSEL where we want to write to */
    *(volatile u32*)ioapic_base = offset;
    /* write the value to IOWIN */
    *(volatile u32*)(ioapic_base + IOAPIC_DATA) = val; 
}
 
static inline u32 read_reg(const u8 offset)
{
    /* tell IOREGSEL where we want to read from */
    *(volatile u32*)ioapic_base = offset;
    /* return the data from IOWIN */
    return *(volatile u32*)(ioapic_base + IOAPIC_DATA);
}

void io_apic(uintptr_t base)
{
    ioapic_base = base;
    map_page((void*)ioapic_base, (void*)ioapic_base, 0x13);

    *(volatile u32*)ioapic_base = IOAPIC_ID;
    printf("IO APIC ID: %x\n", *(volatile u32*)(ioapic_base + IOAPIC_DATA));
    *(volatile u32*)ioapic_base = IOAPIC_VER;
    printf("IO APIC Version: %x\n", *(volatile u32*)(ioapic_base + IOAPIC_DATA));
    *(volatile u32*)ioapic_base = IOAPIC_ARB;
    printf("IO APIC Arbitration: %x\n", *(volatile u32*)(ioapic_base + IOAPIC_DATA));
}

void io_apic_entry(const u8 irq, const u8 vector, const u8 flags, const u8 dest)
{
    redir_entry_t entry;
    entry.vector = vector;
    entry.flags = flags;
    entry.mask = 0;
    entry.dest = dest;

    printf("IO APIC Entry: %x %x %x %x\n", *(u32*)&entry, *((u32*)&entry + 1), irq, IOAPIC_REDTBL(irq));
    write_reg(IOAPIC_REDTBL(irq), *(u32*)&entry);
    write_reg(IOAPIC_REDTBL(irq) + 1, *((u32*)&entry + 1));
}
