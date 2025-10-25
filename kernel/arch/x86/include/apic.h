// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef x86_APIC_H
#define x86_APIC_H

#include <lilac/types.h>
#include <acpi/madt.h>

#define APIC_REG_ID      0x020
#define APIC_REG_VER     0x030
#define APIC_REG_TPR     0x080
#define APIC_REG_APR     0x090
#define APIC_REG_PPR     0x0A0
#define APIC_REG_EOI     0x0B0
#define APIC_REG_RRD     0x0C0
#define APIC_REG_LDR     0x0D0
#define APIC_REG_DFR     0x0E0
#define APIC_REG_SPUR    0x0F0
#define APIC_REG_ISR     0x100
#define APIC_REG_TMR     0x180
#define APIC_REG_IRR     0x200
#define APIC_REG_ERR     0x280
#define APIC_REG_CMCI    0x2F0

#define APIC_ICR_DATA    0x300
#define APIC_ICR_SELECT  0x310
#define APIC_LVT_TIMER   0x320
#define APIC_LVT_THERMAL 0x330
#define APIC_LVT_PERF    0x340
#define APIC_LVT_LINT0   0x350
#define APIC_LVT_LINT1   0x360
#define APIC_LVT_ERROR   0x370

#define APIC_TIMER_INIT  0x380
#define APIC_TIMER_CURR  0x390
#define APIC_TIMER_DIV   0x3E0

#define APIC_DELIV_LOWPRI    (1<<8)
#define APIC_DELIV_SMI       (2<<8)
#define APIC_DELIV_NMI       (4<<8)
#define APIC_DELIV_EXTINT    (7<<8)
#define APIC_DELIV_INIT      (5<<8)

#define APIC_DELIV_STATUS    (1<<12)

#define APIC_POLARITY_HIGH   (1<<13)
#define APIC_TRIGGER_LEVEL   (1<<15)

#define APIC_LVT_DISABLE     (1<<16)
#define APIC_LVT_MASKED      (1<<16)
#define APIC_TIMER_PERIODIC  (1<<17)
#define APIC_TIMER_DEADLINE  (1<<18)


#define APIC_TIMER_BASEDIV   (1<<20)

#define APIC_ESR_SEND_CHECKSUM (1<<0)
#define APIC_ESR_RECV_CHECKSUM (1<<1)
#define APIC_ESR_SEND_ACCEPT   (1<<2)
#define APIC_ESR_RECV_ACCEPT   (1<<3)
#define APIC_ESR_REDIR_IPI     (1<<4)
#define APIC_ESR_SEND_ILLEGAL  (1<<5)
#define APIC_ESR_RECV_ILLEGAL  (1<<6)
#define APIC_ESR_ILL_REG_ADDR  (1<<7)

#define CPUID_6_EAX_ARAT (1<<2) // 1 if APIC timer is always running even in deep c states

void apic_init(struct madt_info *madt);
void lapic_enable(uintptr_t apic_base);
void ap_lapic_enable(void);
void ioapic_init(struct ioapic *ioapic, struct int_override *over, u8 num_over);
void ioapic_entry(u8 irq, u8 vector, u8 flags, u8 dest);
void apic_eoi(void);
int ap_init(void);
u8 get_lapic_id(void);

void apic_tsc_deadline(void);
void tsc_deadline_set(u64 ns_from_now);
void apic_periodic(u32 ms);

#endif
