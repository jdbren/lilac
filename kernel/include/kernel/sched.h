// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _KERNEL_SCHED_H
#define _KERNEL_SCHED_H

void yield(void);
void schedule(void);
void sched_init(int timer);
struct task *create_process(const char *path);
void schedule_task(struct task *new_task);
struct task *get_current_task(void);


#endif
