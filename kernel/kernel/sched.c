// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/sched.h>

#include <stdbool.h>
#include <lilac/lilac.h>
#include <lilac/log.h>
#include <lilac/process.h>
#include <lilac/syscall.h>
#include <mm/kmm.h>

struct rq {
    spinlock_t lock;
    u8 cpu;
    u32 nr_running;

    struct task *curr;
    struct task *next;
    struct task *idle;

    struct rb_root_cached queue;
};


void idle(void)
{
    klog(LOG_DEBUG, "Idle task started, pid = %d\n", current->pid);
    arch_idle();
}

static struct task *task_queue[16];
static struct task root = {
    .state = TASK_RUNNING,
    .name = "idle",
    .priority = 20,
    .pid = 0,
    .ppid = 0
};
static int back;
static volatile int current_task;
volatile int sched_timer = -1;
int timer_reset = -1;

extern void arch_context_switch(struct task *prev, struct task *next);

struct task* get_current_task(void)
{
    return task_queue[current_task];
}

void remove_task(struct task *p)
{
    for (int i = 0; i < 16; i++) {
        if (task_queue[i] == p) {
            task_queue[i] = NULL;
            klog(LOG_DEBUG, "Task %d removed from queue\n", p->pid);
            break;
        }
    }
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
    root.pgd = arch_get_pgd();
    struct task *pid1 = init_process();
    schedule_task(pid1);
    timer_reset = 100;
    kstatus(STATUS_OK, "Scheduler initialized\n");
}

void sched_clock_init(void)
{
    yield();
}

static void context_switch(struct task *prev, struct task *next)
{
    register uintptr_t rsp asm("rsp");
    klog(LOG_DEBUG, "Switching from task %d to task %d\n", prev->pid, next->pid);
#ifdef DEBUG_SCHED
    klog(LOG_DEBUG, "Current stack pointer: %p\n", rsp);
    klog(LOG_DEBUG, "Previous task info: \n");
    klog(LOG_DEBUG, "\tPID: %d\n", prev->pid);
    klog(LOG_DEBUG, "\tPPID: %d\n", prev->ppid);
    klog(LOG_DEBUG, "\tPGD: %p\n", prev->pgd);
    klog(LOG_DEBUG, "\tPC: %p\n", prev->pc);
    klog(LOG_DEBUG, "\tStack: %p\n", prev->kstack);
    klog(LOG_DEBUG, "Next task info: \n");
    klog(LOG_DEBUG, "\tPID: %d\n", next->pid);
    klog(LOG_DEBUG, "\tPPID: %d\n", next->ppid);
    klog(LOG_DEBUG, "\tPGD: %p\n", next->pgd);
    klog(LOG_DEBUG, "\tPC: %p\n", next->pc);
    klog(LOG_DEBUG, "\tStack: %p\n", next->kstack);
#endif
    arch_disable_interrupts();
    save_fp_regs(prev);
    arch_context_switch(prev, next);
    restore_fp_regs(prev);
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
        if (prev->state == TASK_RUNNING)
            return;
        else
            current_task = 0; // idle
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

struct task * find_by_pid(u32 pid)
{
    for (int i = 0; i < 16; i++) {
        if (task_queue[i] && task_queue[i]->pid == pid)
            return task_queue[i];
    }
    return NULL;
}

long waitpid(int pid)
{
    struct task *p = find_by_pid(pid);
    if (!p)
        return -ECHILD;
    if (p->ppid != current->pid)
        return -ECHILD;
    klog(LOG_DEBUG, "Process %d: Waiting for task %d\n", get_pid(), pid);
    current->state = TASK_SLEEPING;
    p->parent_wait = true;
    schedule();
    arch_disable_interrupts();
    reap_task(p);
    remove_task(p);
    kfree(p);
    arch_enable_interrupts();
    klog(LOG_DEBUG, "Task %d has exited, continuing task %d\n", pid, get_pid());
    return 0;
}
SYSCALL_DECL1(waitpid, int, pid)
{
    return waitpid(pid);
}

void wakeup(int pid)
{
    struct task *p = find_by_pid(pid);
    if (!p)
        return;
    p->state = TASK_RUNNING;
    klog(LOG_DEBUG, "Waking up task %d\n", pid);
}

void wakeup_task(struct task *p)
{
    if (p->state == TASK_SLEEPING) {
        p->state = TASK_RUNNING;
        klog(LOG_DEBUG, "Waking up task %d\n", p->pid);
    }
}
