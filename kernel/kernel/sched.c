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
    struct task *idle;

    struct rb_root_cached queue;
};

struct waitqueue {
    spinlock_t lock;
    struct list_head task_list;
};

struct wq_entry {
    struct task *task;
    //wait_queue_func_t func; // callback function, e.g. try_to_wake_up()
    struct list_head entry;
};

static struct rq rqs[4] = {
    [0 ... 3] = {
        .lock = SPINLOCK_INIT,
        .cpu = 0,
        .nr_running = 0,
        .curr = NULL,
        .idle = NULL,
        .queue = RB_ROOT_CACHED,
    }
};

static struct waitqueue wait_q = {
    .lock = SPINLOCK_INIT,
    .task_list = LIST_HEAD_INIT(wait_q.task_list),
};

void sleep_task(struct task *p)
{
    if (p->state == TASK_RUNNING) {
        rb_erase_cached(&p->rq_node, &rqs[0].queue);
        p->state = TASK_SLEEPING;
        rqs[0].nr_running--;
        struct wq_entry *wait = kmalloc(sizeof(*wait));
        wait->task = p;
        list_add_tail(&wait->entry, &wait_q.task_list);
        klog(LOG_DEBUG, "Task %d is now sleeping\n", p->pid);
    }
}

void idle(void)
{
    klog(LOG_DEBUG, "Idle task started, pid = %d\n", current->pid);
    arch_idle();
}

static struct task root = {
    .state = TASK_RUNNING,
    .name = "idle",
    .priority = 20,
    .pid = 0,
    .ppid = 0,
    .lock = SPINLOCK_INIT,
};

volatile int sched_timer = -1;
int timer_reset = -1;

extern void arch_context_switch(struct task *prev, struct task *next);

struct task* get_current_task(void)
{
    return rqs[0].curr;
}

void remove_task(struct task *p)
{
    rb_erase_cached(&p->rq_node, &rqs[0].queue);
}

void yield(void)
{
    sched_timer = timer_reset;
    schedule();
}

static bool prio_comp(struct rb_node *a, const struct rb_node *b)
{
    struct task *task_a = rb_entry(a, struct task, rq_node);
    struct task *task_b = rb_entry(b, struct task, rq_node);
    return task_a->priority < task_b->priority;
}


void schedule_task(struct task *new_task)
{
    struct task *tmp = NULL;
    rqs[0].nr_running++;
    rb_add_cached(&new_task->rq_node, &rqs[0].queue, prio_comp);
#ifdef DEBUG_SCHED
    struct task *t = new_task;
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

struct task * find_by_pid(int pid)
{
    struct task *t = NULL, *tmp = NULL;
    rbtree_postorder_for_each_entry_safe(t, tmp, &rqs[0].queue.rb_root, rq_node) {
        if (t->pid == pid)
            return t;
    }
    return NULL;
}

static struct wq_entry *find_waiting_task(int pid)
{
    struct wq_entry *wait = NULL;
    list_for_each_entry(wait, &wait_q.task_list, entry) {
        if (wait->task->pid == pid)
            return wait;
    }
    return NULL;
}

long wait_task(struct task *p)
{
    klog(LOG_DEBUG, "Process %d: Waiting for task %d\n", get_pid(), p->pid);
    rb_erase_cached(&current->rq_node, &rqs[0].queue);
    current->state = TASK_SLEEPING;
    struct wq_entry *wait = kmalloc(sizeof(*wait));
    wait->task = current;
    list_add_tail(&wait->entry, &wait_q.task_list);
    p->parent_wait = true;

    schedule();

    arch_disable_interrupts();
    reap_task(p);
    kfree(p);
    // arch_enable_interrupts();
    klog(LOG_DEBUG, "Task %d has exited, continuing task %d\n", p->pid, get_pid());
    return 0;
}
SYSCALL_DECL1(waitpid, int, pid)
{
    struct task *p = find_by_pid(pid);
    if (!p)
        return -ECHILD;
    if (p->ppid != current->pid)
        return -ECHILD;
    return wait_task(p);
}

void wakeup_task(struct task *p)
{
    if (p->state == TASK_SLEEPING) {
        p->state = TASK_RUNNING;
        rb_add_cached(&p->rq_node, &rqs[0].queue, prio_comp);
        rqs[0].nr_running++;
        klog(LOG_DEBUG, "Waking up task %d\n", p->pid);
    }
}

void wakeup(int pid)
{
    struct wq_entry *wq_ent = find_waiting_task(pid);
    if (!wq_ent)
        return;
    list_del(&wq_ent->entry);
    wakeup_task(wq_ent->task);
    kfree(wq_ent);
}
