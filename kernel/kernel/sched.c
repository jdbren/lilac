// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/sched.h>

#include <lilac/lilac.h>
#include <lilac/process.h>
#include <lilac/syscall.h>
#include <lilac/percpu.h>
#include <lilac/timer.h>
#include <mm/kmm.h>

#define MIN_GRANULARITY 2

extern uintptr_t stack_top;

static struct mm_info root_mm = {
    .kstack = (void*)((uintptr_t)&stack_top - __KERNEL_STACK_SZ),
};

static struct sighandlers root_sighand = {
    .ref_count = 1,
    .lock = SPINLOCK_INIT,
    .actions = {
        [SIGINT] = {.sa.sa_handler = SIG_IGN, .sa.sa_flags = SA_RESTART},
        [SIGQUIT] = {.sa.sa_handler = SIG_IGN, .sa.sa_flags = SA_RESTART},
        [SIGTERM] = {.sa.sa_handler = SIG_IGN, .sa.sa_flags = SA_RESTART},
        [SIGCHLD] = {.sa.sa_handler = SIG_DFL, .sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT | SA_RESTART},
        [SIGKILL] = {.sa.sa_handler = SIG_IGN, .sa.sa_flags = SA_RESTART},
    }
};

struct task __rootp = {
    .state = TASK_RUNNING,
    .name = "idle",
    .priority = 20,
    .pid = 0,
    .ppid = 0,
    .lock = SPINLOCK_INIT,
    .mm = &root_mm,
    .sighandlers = &root_sighand,
};

struct rq {
    spinlock_t lock;
    u8 cpu;
    u32 nr_running;

    struct task *curr;
    struct task *idle;

    struct rb_root_cached queue;
};

static struct rq rqs[CONFIG_MAX_CPUS] = {
    [0 ... CONFIG_MAX_CPUS-1] = {
        .lock = SPINLOCK_INIT,
        .cpu = 0,
        .nr_running = 0,
        .curr = &__rootp,
        .idle = &__rootp,
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

extern void __context_switch_asm(struct task *prev, struct task *next);

struct task* get_current_task(void)
{
    return rqs[this_cpu_id()].curr;
}

struct rq *this_cpu_rq(void)
{
    return &rqs[this_cpu_id()];
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
    return task_a->vruntime < task_b->vruntime;
}

void __rq_del(struct rq *rq, struct task *p)
{
    rb_erase_cached(&p->rq_node, &rq->queue);
    rq->nr_running--;
    p->on_rq = false;
}

void rq_del(struct task *p)
{
    if (!p->on_rq) {
        klog(LOG_ERROR, "Task %d is not on the run queue\n", p->pid);
        return;
    }
#ifdef DEBUG_SCHED
    klog(LOG_DEBUG, "Removing task %d from run queue\n", p->pid);
#endif
    struct rq *rq = this_cpu_rq();
    acquire_lock(&rq->lock);
    __rq_del(rq, p);
    release_lock(&rq->lock);
}

void __rq_add(struct rq *rq, struct task *p)
{
    rb_add_cached(&p->rq_node, &rq->queue, prio_comp);
    rq->nr_running++;
    p->on_rq = true;
}

void rq_add(struct task *p)
{
    if (p->on_rq) {
        klog(LOG_ERROR, "Task %d is already on the run queue\n", p->pid);
        return;
    }
#ifdef DEBUG_SCHED
    klog(LOG_DEBUG, "Adding task %d to run queue\n", p->pid);
#endif
    struct rq *rq = this_cpu_rq();
    acquire_lock(&rq->lock);
    __rq_add(rq, p);
    release_lock(&rq->lock);
}

void set_task_running(struct task *p)
{
    if (p->state == TASK_ZOMBIE) {
        panic("Tried to wake up a zombie process %d\n", p->pid);
    }

    if (p->state != TASK_RUNNING) {
        p->state = TASK_RUNNING;
        if (!p->on_rq)
            rq_add(p);
        klog(LOG_DEBUG, "Waking up task %d\n", p->pid);
    }
}

void set_task_sleeping(struct task *p)
{
    if (p->state == TASK_RUNNING) {
        if (p->on_rq)
            rq_del(p);
        p->state = TASK_SLEEPING;
        klog(LOG_DEBUG, "Task %d is now sleeping\n", p->pid);
    }
}

void set_task_uninterruptible(struct task *p)
{
    if (p->state == TASK_RUNNING) {
        if (p->on_rq)
            rq_del(p);
        p->state = TASK_UNINTERRUPTIBLE;
        klog(LOG_DEBUG, "Task %d is now uninterruptible\n", p->pid);
    }
}

void set_current_state(u8 state)
{
    switch (state) {
        case TASK_RUNNING:
            set_task_running(current);
            break;
        case TASK_SLEEPING:
            set_task_sleeping(current);
            break;
        case TASK_UNINTERRUPTIBLE:
            set_task_uninterruptible(current);
            break;
        case TASK_ZOMBIE:
            current->state = TASK_ZOMBIE;
            if (current->on_rq)
                rq_del(current);
            break;
    }
}

void yield(void)
{
    sched_timer = timer_reset;
    schedule();
}

void schedule_task(struct task *new_task)
{
    new_task->state = TASK_RUNNING;
    new_task->vruntime = current->vruntime + MIN_GRANULARITY;
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
    rqs[0].idle = &__rootp;
    rqs[0].curr = &__rootp;
    rqs[0].nr_running = 1;
    __rootp.pgd = arch_get_pgd();
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
    this_cpu_rq()->curr = next;
    save_fp_regs(prev);
    arch_prepare_context_switch(next);
    __context_switch_asm(prev, next);
    restore_fp_regs(prev);
}

void schedule(void)
{
    struct task *cur = current;
    struct task *next;
    struct rq *rq = this_cpu_rq();

    acquire_lock(&rq->lock);

    struct rb_node *node = rb_first_cached(&rq->queue);
    if (!node) {
        if (cur->state == TASK_RUNNING)
            next = cur;
        else
            next = rq->idle;
    } else {
        next = rb_entry(node, struct task, rq_node);
        __rq_del(rq, next);
    }

    if (cur->state == TASK_RUNNING && cur != rq->idle) {
        __rq_add(rq, cur);
    }

    release_lock(&rq->lock);

    if (next != cur) {
        context_switch(cur, next);
    }
}


void sched_tick(void)
{
    struct task *cur = current;
    struct rq *rq = this_cpu_rq();
    if (unlikely(sched_timer == -1))
        return;

    u64 now = read_ticks();
    if (cur->state == TASK_RUNNING) {
        u64 delta_exec = now - cur->exec_started;

        if (delta_exec) {
            cur->exec_started = now;
            cur->runtime += ticks_to_ns(delta_exec);
            cur->vruntime += delta_exec; // * nice factor / weight;
        }
    }

    acquire_lock(&rq->lock);

    struct task *leftmost = NULL;
    struct rb_node *node = rb_first_cached(&rq->queue);

    if (node)
        leftmost = rb_entry(node, struct task, rq_node);

    if (leftmost && leftmost != cur) {
        s64 diff = (s64)(cur->vruntime - leftmost->vruntime);

        if (diff > MIN_GRANULARITY)
            cur->flags.need_resched = 1;
    }

    release_lock(&rq->lock);
}

SYSCALL_DECL0(sched_yield)
{
    yield();
    return 0;
}
