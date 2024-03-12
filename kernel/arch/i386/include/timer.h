// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef x86_TIMER_H
#define x86_TIMER_H

#include <kernel/types.h>

#define HPET_ID_REG 0x0
#define HPET_CONFIG_REG 0x10
#define HPET_INTR_REG 0x20
#define HPET_COUNTER_REG 0xf0
#define HPET_TIMER_CONF_REG(N) (0x100 + 0x20 * N)
#define HPET_TIMER_COMP_REG(N) (0x108 + 0x20 * N)


struct hpet_id_reg {
    u8 rev_id;
    u8 flags;
    u16 vendor_id;
    u32 counter_clk_period;
} __attribute__((packed));

void hpet_init(u32 time, struct hpet_info *info);
void timer_init(u32 ms, struct hpet_info *info);
void sleep(u32 millis);
unsigned read_pit_count(void);
void set_pit_count(unsigned count);

#endif
