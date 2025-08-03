// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef x86_TIMER_H
#define x86_TIMER_H

#include <lilac/types.h>
#include <acpi/hpet.h>

void timer_init(u32 ms, struct hpet_info *info);
long long rtc_init(void);
u64 rdtsc(void);

#endif
