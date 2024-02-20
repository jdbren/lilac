#ifndef _KERNEL_SCHED_H
#define _KERNEL_SCHED_H

void schedule(void);
void init_sched(int timer);
struct task *create_process(const char *path);
void schedule_task(struct task *new_task);
struct task *get_current_task(void);

#endif