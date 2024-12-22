// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/types.h>
#include <lilac/log.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/syscall.h>

static struct task* task_queue[16];
static struct task root = {
    .state = TASK_RUNNING,
    .name = "root",
    .priority = 20
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
    klog(LOG_DEBUG, "Task_queue[%d] = %s\n", back, new_task->name);
}

void sched_init(void)
{
    task_queue[0] = &root;
    struct task *pid1 = init_process();
    schedule_task(pid1);
    timer_reset = 3000;
    kstatus(STATUS_OK, "Scheduler initialized\n");
}

void sched_clock_init(void)
{
    yield();
}

static void context_switch(struct task *prev, struct task *next)
{
    klog(LOG_DEBUG, "Next task info: \n");
    klog(LOG_DEBUG, "\tPID: %d\n", next->pid);
    klog(LOG_DEBUG, "\tPPID: %d\n", next->ppid);
    klog(LOG_DEBUG, "\tPGD: %x\n", next->pgd);
    klog(LOG_DEBUG, "\tPC: %x\n", next->pc);
    klog(LOG_DEBUG, "\tStack: %x\n", next->stack);

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
        if (task_queue[current_task]) {
            if (task_queue[current_task]->state == TASK_RUNNING &&
                task_queue[current_task]->priority <= prev->priority)
                break;
        }
        i++;
    }
    if (task_queue[current_task] == prev) {
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

    schedule();
}

int find_by_pid(int pid)
{
    for (int i = 0; i < 16; i++) {
        if (task_queue[i] && task_queue[i]->pid == pid)
            return i;
    }
    return -1;
}

long waitpid(int pid)
{
    int i = find_by_pid(pid);
    if (i == -1)
        return -1;
    struct task *task = task_queue[i];
    klog(LOG_DEBUG, "Waiting for task %d\n", pid);
    if (task == NULL)
        return -1;
    while (task->state != TASK_DEAD)
        yield();
    return 0;
}
SYSCALL_DECL1(waitpid, int, pid)
