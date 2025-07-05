// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/sched.h>

#include <stdbool.h>
#include <lilac/lilac.h>
#include <lilac/process.h>
#include <lilac/syscall.h>
#include <mm/kmm.h>

static struct task root = {
    .state = TASK_RUNNING,
    .name = "idle",
    .priority = 20,
    .pid = 0,
    .ppid = 0,
    .lock = SPINLOCK_INIT,
};

struct rq {
    spinlock_t lock;
    u8 cpu;
    u32 nr_running;

    struct task *curr;
    struct task *idle;

    struct rb_root_cached queue;
};

static struct rq rqs[4] = {
    [0 ... 3] = {
        .lock = SPINLOCK_INIT,
        .cpu = 0,
        .nr_running = 0,
        .curr = &root,
        .idle = &root,
        .queue = RB_ROOT_CACHED,
    }
};


void idle(void)
{
    klog(LOG_DEBUG, "Idle task started, pid = %d\n", current->pid);
    arch_idle();
}


volatile int sched_timer = -1;
int timer_reset = -1;

extern void arch_context_switch(struct task *prev, struct task *next);

struct task* get_current_task(void)
{
    return rqs[0].curr;
}

struct task * find_child_by_pid(struct task *parent, int pid)
{
    struct task *p = NULL;
    list_for_each_entry(p, &parent->children, sibling) {
        if (p->pid == pid)
            return p;
    }
    return NULL;
}

static bool prio_comp(struct rb_node *a, const struct rb_node *b)
{
    struct task *task_a = rb_entry(a, struct task, rq_node);
    struct task *task_b = rb_entry(b, struct task, rq_node);
    return task_a->priority < task_b->priority;
}

void rq_del(struct task *p)
{
    if (!p->on_rq) {
        klog(LOG_ERROR, "Task %d is not on the run queue\n", p->pid);
        return;
    }
    acquire_lock(&rqs[0].lock);
    rb_erase_cached(&p->rq_node, &rqs[0].queue);
    rqs[0].nr_running--;
    p->on_rq = false;
    release_lock(&rqs[0].lock);
}

void rq_add(struct task *p)
{
    if (p->on_rq) {
        klog(LOG_ERROR, "Task %d is already on the run queue\n", p->pid);
        return;
    }
    acquire_lock(&rqs[0].lock);
    rb_add_cached(&p->rq_node, &rqs[0].queue, prio_comp);
    rqs[0].nr_running++;
    p->on_rq = true;
    release_lock(&rqs[0].lock);
}

void set_task_running(struct task *p)
{
    if (p->state == TASK_SLEEPING) {
        p->state = TASK_RUNNING;
        rq_add(p);
        klog(LOG_DEBUG, "Waking up task %d\n", p->pid);
    }
}

void set_task_sleeping(struct task *p)
{
    if (p->state == TASK_RUNNING) {
        rq_del(p);
        p->state = TASK_SLEEPING;
        klog(LOG_DEBUG, "Task %d is now sleeping\n", p->pid);
    }
}

void yield(void)
{
    sched_timer = timer_reset;
    schedule();
}

void schedule_task(struct task *new_task)
{
    rq_add(new_task);
#ifdef DEBUG_SCHED
    struct task *tmp = NULL, *t = new_task;
    rbtree_postorder_for_each_entry_safe(t, tmp, &rqs[0].queue.rb_root, rq_node) {
        klog(LOG_DEBUG, "RB: Task %d in queue\n", t->pid);
    }
#endif
}

void sched_init(void)
{
    rqs[0].idle = &root;
    rqs[0].curr = &root;
    rqs[0].nr_running = 1;
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
    arch_disable_interrupts();
    rqs[0].curr = next;
    klog(LOG_DEBUG, "Switching from task %d to task %d\n", prev->pid, next->pid);
#ifdef DEBUG_SCHED
    register uintptr_t rsp asm("rsp");
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
    save_fp_regs(prev);
    arch_context_switch(prev, next);
    restore_fp_regs(prev);
}

void schedule(void)
{
    struct task *prev = current;
    struct task *next = NULL;

    struct rb_node *node = rb_first_cached(&rqs[0].queue);
    if (!node)
        next = rqs[0].idle;
    else
        next = rb_entry(node, struct task, rq_node);
    if (next == prev) {
        if (prev->state == TASK_RUNNING)
            return;
        else
            next = rqs[0].idle; // idle
    }

    if (next->state != TASK_RUNNING) {
        klog(LOG_FATAL, "Task %d is not running\n", next->pid);
        kerror("");
    }

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
