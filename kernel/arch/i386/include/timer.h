// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef x86_TIMER_H
#define x86_TIMER_H

#include <kernel/types.h>
#include <acpi/hpet.h>

void hpet_init(u32 time, struct hpet_info *info);
void timer_init(u32 ms, struct hpet_info *info);

#endif
