// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <kernel/types.h>
#include <kernel/process.h>
#include <kernel/sched.h>

static struct task* task_queue[16];
static int back;
static int current_task;
int sched_timer = -1;
int timer_reset = -1;

extern void arch_context_switch(struct task *prev, struct task *next);

struct task* get_current_task(void)
{
    return task_queue[current_task];
}

void yield(void)
{
    sched_timer = timer_reset;
    schedule();
}

void schedule_task(struct task *new_task)
{
    task_queue[back++] = new_task;
    printf("Task_queue[%d] = %s\n", back - 1, new_task->name);
}

void init_sched(int timer)
{
    struct task *pid1 = init_process();
    back = 0;
    current_task = 0;
    schedule_task(pid1);
    timer_reset = timer;
    sched_timer = timer;
}

static void context_switch(struct task *prev, struct task *next)
{
    arch_context_switch(prev, next);
}

void schedule()
{
    if (back == 0)
        return;

    struct task *prev = task_queue[current_task];

    int i = 0;
    while (i < 16) {
        current_task = (current_task + 1) % 16;
        if (task_queue[current_task] != NULL)
            break;
        i++;
    }
    if (task_queue[current_task] == prev) {
        printf("Running task %s\n", prev->name);
        return;
    }
    struct task *next = task_queue[current_task];

    context_switch(prev, next);
}

void sched_tick()
{
    printf("Running scheduler\n");
    sched_timer = timer_reset;
    schedule();
}
