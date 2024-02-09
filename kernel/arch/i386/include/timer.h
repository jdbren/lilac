#ifndef _TIMER_H
#define _TIMER_H

#include <kernel/types.h>

void timer_init(void);
void sleep(u32 millis);
unsigned read_pit_count(void);
void set_pit_count(unsigned count);

#endif
