// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef x86_TIMER_H
#define x86_TIMER_H

#include <lilac/types.h>
#include <acpi/hpet.h>

void timer_init(struct hpet_info *info);
void timer_tick_init(void);
long long rtc_init(void);
u64 rdtsc(void);

#endif
