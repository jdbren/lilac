// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#ifndef _KERNEL_SCHED_H
#define _KERNEL_SCHED_H

#ifdef __cplusplus
extern "C" {
#endif

#include <lilac/process.h>

void sched_init(void);
void sched_clock_init(void);
void schedule(void);
void sched_tick(void);
void yield(void);
void schedule_task(struct task *new_task);
struct task * get_current_task(void);
struct task * find_child_by_pid(struct task *parent, int pid);

#define current get_current_task()

int do_fork(void);
void idle(void);

void rq_add(struct task *p);
void rq_del(struct task *p);
void set_task_running(struct task *p);
void set_task_sleeping(struct task *p);
void set_current_state(u8 state);

static inline bool task_interrupted_ack(void)
{
    if (current->flags.interrupted) {
        current->flags.interrupted = 0;
        return true;
    }
    return false;
}

#ifdef __cplusplus
}
#endif

#endif
