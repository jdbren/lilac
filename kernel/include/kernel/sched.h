// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _KERNEL_SCHED_H
#define _KERNEL_SCHED_H

void sched_init(int timer);
void schedule(void);
void yield(void);
void schedule_task(struct task *new_task);
struct task *get_current_task(void);

#endif
