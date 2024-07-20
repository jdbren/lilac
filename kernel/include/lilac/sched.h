// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _KERNEL_SCHED_H
#define _KERNEL_SCHED_H

struct task;

void sched_init(void);
void sched_clock_init(void);
void schedule(void);
void yield(void);
void schedule_task(struct task *new_task);
struct task *get_current_task(void);

#define current get_current_task()

#endif
