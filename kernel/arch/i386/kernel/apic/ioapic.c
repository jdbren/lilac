// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <string.h>
#include <kernel/types.h>
#include <acpi/madt.h>
#include <mm/kheap.h>
#include "apic.h"
#include "paging.h"

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
static u8 ioapic_gsi_base;
static struct int_override *overrides;
static int num_overrides;

static u8 get_redir_num(u8 irq)
{
    int i = 0;
    while (i < num_overrides) {
        if (overrides[i].source == irq)
            return overrides[i].global_system_interrupt;
        i++;
    }
    return irq;
}

static inline void write_reg(const u8 offset, const u32 val)
{
    *(volatile u32*)ioapic_base = offset;
    *(volatile u32*)(ioapic_base + IOAPIC_DATA) = val;
}

static inline u32 read_reg(const u8 offset)
{
    *(volatile u32*)ioapic_base = offset;
    return *(volatile u32*)(ioapic_base + IOAPIC_DATA);
}

void ioapic_init(struct ioapic *ioapic, struct int_override *over, u8 num_over)
{
    ioapic_base = ioapic->address;
    ioapic_gsi_base = ioapic->gsi_base;
    num_overrides = num_over;
    overrides = kcalloc(num_over, sizeof(struct int_override));
    for (int i = 0; i < num_over; over = over->next, i++) {
        memcpy(overrides + i, over, sizeof(struct int_override));
        overrides[i].next = 0;
    }
    map_page((void*)ioapic_base, (void*)ioapic_base, PG_CACHE_DISABLE | PG_WRITE);
    printf("IOAPIC initialized\n");
}

void ioapic_entry(u8 irq, u8 vector, u8 flags, u8 dest)
{
    irq = get_redir_num(irq);
    redir_entry_t entry;
    entry.vector = vector;
    entry.flags = flags;
    entry.mask = 0;
    entry.dest = dest;

    write_reg(IOAPIC_REDTBL(irq), *(u32*)&entry);
    write_reg(IOAPIC_REDTBL(irq) + 1, *((u32*)&entry + 1));
}
