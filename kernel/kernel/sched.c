// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/types.h>
#include <lilac/process.h>
#include <lilac/sched.h>

static struct task* task_queue[16];
static struct task root = {
    .pid = 0,
    .ppid = 0,
    .pgd = 0,
    .pc = 0,
    .stack = 0,
    .time_slice = 0,
    .parent = NULL,
    .fs = NULL,
    .files = NULL,
    .info = NULL,
    .priority = 0,
    .state = TASK_DEAD,
    .name = "root"
};
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
    task_queue[++back] = new_task;
    // printf("Task_queue[%d] = %s\n", back, new_task->name);
}

void sched_init(void)
{
    task_queue[0] = &root;
    struct task *pid1 = init_process();
    back = 0;
    current_task = 0;
    schedule_task(pid1);
    timer_reset = 1000;
    printf("Scheduler initialized\n");
}

void sched_clock_init(void)
{
    sched_timer = timer_reset;
}

static void context_switch(struct task *prev, struct task *next)
{
    printf("Next task info: \n");
    printf("\tPID: %d\n", next->pid);
    printf("\tPPID: %d\n", next->ppid);
    printf("\tPGD: %x\n", next->pgd);
    printf("\tPC: %x\n", next->pc);
    printf("\tStack: %x\n", next->stack);

    arch_context_switch(prev, next);
}

void schedule(void)
{
    if (back == 0)
        return;

    struct task *prev = task_queue[current_task];

    int i = 0;
    while (i < 16) {
        current_task = (current_task + 1) % 16;
        if (task_queue[current_task] != NULL) {
            if (task_queue[current_task]->state == TASK_RUNNING)
                break;
            else if (task_queue[current_task]->state == TASK_DEAD)
                task_queue[current_task] = NULL;
        }
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
    if (sched_timer == -1)
        return;
    if (sched_timer > 0) {
        sched_timer--;
        return;
    }
    sched_timer = timer_reset;
    //printf("Running scheduler\n");
    schedule();
}
