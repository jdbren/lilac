// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef x86_TIMER_H
#define x86_TIMER_H

#include <kernel/types.h>

void timer_init(void);
void sleep(u32 millis);
unsigned read_pit_count(void);
void set_pit_count(unsigned count);

#endif
