#include <lilac/wait.h>

#include <lilac/lilac.h>
#include <lilac/sched.h>
#include <lilac/syscall.h>
#include <lilac/uaccess.h>
#include <lilac/signal.h>

#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"

static struct waitqueue wait_q = {
    .lock = SPINLOCK_INIT,
    .task_list = LIST_HEAD_INIT(wait_q.task_list),
};

static inline void __add_wait_entry(struct wq_entry *wait, struct waitqueue *wq)
{
    list_add_tail(&wait->entry, &wq->task_list);
}

static inline void __remove_wait_entry(struct wq_entry *wait)
{
    list_del_init(&wait->entry);
}

static void remove_wait_entry(struct wq_entry *wait, struct waitqueue *wq)
{
    unsigned long flags;
    acquire_lock_irqsave(&wq->lock, &flags);
    __remove_wait_entry(wait);
    release_lock_irqrestore(&wq->lock, flags);
}

static struct wq_entry *find_waiting_task(struct waitqueue *wq, int pid)
{
    struct wq_entry *wait = NULL;
    if (list_empty(&wq->task_list)) {
        klog(LOG_DEBUG, "No tasks waiting in waitqueue for pid %d\n", pid);
        return NULL;
    }
    list_for_each_entry(wait, &wq->task_list, entry) {
        if (wait->task->pid == pid)
            return wait;
    }
    return NULL;
}


static inline bool is_thread(struct task *p)
{
    return p->exit_signal == -1 && p != p->tg_leader;
}

// pgid 0 matches any child
static inline bool child_in_pgrp(struct task *child, pid_t pgid)
{
    return !is_thread(child) && (pgid == 0 || child->pgid == pgid);
}

// A zombie leader is waitable only once every thread in its group has exited
static inline bool child_dead(struct task *child)
{
    return READ_ONCE(child->state) == TASK_ZOMBIE &&
        (is_thread(child) || atomic_load(&child->tg_live) == 0);
}

static struct task * find_exited_child(struct task *parent, pid_t pgid)
{
    struct task *child, *found = NULL;
    unsigned long flags;

    acquire_read_lock_irqsave(&tasklist_lock, &flags);
    list_for_each_entry(child, &parent->children, sibling) {
        if (child_dead(child) && child_in_pgrp(child, pgid)) {
            found = child;
            break;
        }
    }
    release_read_lock_irqrestore(&tasklist_lock, flags);
    return found;
}

static struct task * find_stopped_child(struct task *parent, pid_t pgid)
{
    struct task *child, *found = NULL;
    unsigned long flags;

    acquire_read_lock_irqsave(&tasklist_lock, &flags);
    list_for_each_entry(child, &parent->children, sibling) {
        if (child->state == TASK_STOPPED &&
        child_in_pgrp(child, pgid) &&
        child->flags.state_change) {
            child->flags.state_change = 0;
            found = child;
            break;
        }
    }
    release_read_lock_irqrestore(&tasklist_lock, flags);
    return found;
}

static pid_t handle_exited_child(struct task *child, int *status)
{
    pid_t child_pid = child->pid;
    if (status) {
        *status = child->exit_status;
        klog(LOG_DEBUG, "wait_any: Child %d exited with status %d\n", child_pid, *status);
    }
    // a waitable leader's threads have all exited
    reap_dead_threads();
    reap_task(child);
    return child_pid;
}

static pid_t handle_stopped_child(struct task *child, int *status)
{
    klog(LOG_DEBUG, "wait_any: Child %d has stopped\n", child->pid);
    if (status)
        *status = child->exit_status;
    return child->pid;
}

static bool child_waitable(struct task *p, bool wait_stopped)
{
    return child_dead(p) || (wait_stopped && READ_ONCE(p->state) == TASK_STOPPED);
}

static bool any_child_waitable(struct task *parent, bool wait_stopped, pid_t pgid)
{
    struct task *child;
    unsigned long flags;
    bool found = false;

    acquire_read_lock_irqsave(&tasklist_lock, &flags);
    list_for_each_entry(child, &parent->children, sibling) {
        if (!child_in_pgrp(child, pgid))
            continue;
        if (child_dead(child) ||
                (wait_stopped && READ_ONCE(child->state) == TASK_STOPPED &&
                child->flags.state_change)) {
            found = true;
            break;
        }
    }
    release_read_lock_irqrestore(&tasklist_lock, flags);
    return found;
}

static pid_t wait_for(struct task *p, int *status, bool nohang, bool wait_stopped)
{
    for (;;) {
        reap_dead_threads();

        if (child_dead(p)) {
            return handle_exited_child(p, status);
        } else if (READ_ONCE(p->state) == TASK_STOPPED && wait_stopped) {
            return handle_stopped_child(p, status);
        }

        if (nohang) {
            klog(LOG_DEBUG, "Task %d has not exited yet, returning immediately\n", p->pid);
            return 0;
        }

        klog(LOG_DEBUG, "Process %d: Waiting for task %d\n", get_pid(), p->pid);
        int ret = wait_event_interruptible(wait_q, child_waitable(p, wait_stopped));
        if (ret < 0)
            return ret;
    }
}


static pid_t check_children(int *status, bool wait_stopped, pid_t pgid)
{
    reap_dead_threads();
    struct task *child = find_exited_child(current, pgid);
    if (child)
        return handle_exited_child(child, status);

    if (wait_stopped) {
        child = find_stopped_child(current, pgid);
        if (child)
            return handle_stopped_child(child, status);
    }

    return 0;
}

static bool has_child_in_pgrp(pid_t pgid)
{
    struct task *child;
    unsigned long flags;
    bool found = false;

    acquire_read_lock_irqsave(&tasklist_lock, &flags);
    list_for_each_entry(child, &current->children, sibling) {
        if (child_in_pgrp(child, pgid)) {
            found = true;
            break;
        }
    }
    release_read_lock_irqrestore(&tasklist_lock, flags);
    return found;
}

static pid_t wait_any(int *status, bool nohang, bool wait_stopped, pid_t pgid)
{
    pid_t result;

    if (!has_child_in_pgrp(pgid)) {
        klog(LOG_DEBUG, "Process %d has no children to wait for\n", current->pid);
        return -ECHILD;
    }

    // Check if any child has already exited or stopped
    result = check_children(status, wait_stopped, pgid);
    if (result != 0)
        return result;

    if (nohang)
        return 0;

    for (;;) {
        int ret = wait_event_interruptible(wait_q,
            any_child_waitable(current, wait_stopped, pgid));
        if (ret < 0)
            return ret;

        result = check_children(status, wait_stopped, pgid);
        if (result != 0)
            return result;
    }
}

// TODO: POSIX says when SIGCHLD is SIG_IGN, then wait all children and return ECHILD

SYSCALL_DECL3(waitpid, int, pid, int*, status, int, options)
{
    if (status && !access_ok(status, sizeof(int)))
        return -EFAULT;

    int ret = 0;
    int status_val = 0;
    if (pid <= 0) {
        // -1: any child; 0: our process group; < -1: process group -pid
        pid_t pgid = pid == -1 ? 0 : pid == 0 ? current->pgid : -pid;
        ret = wait_any(&status_val, options & WNOHANG, options & WUNTRACED, pgid);
        if (ret > 0 && status)
            return put_user(status_val, status) ? -EFAULT : ret;
        return ret;
    }

    struct task *p = find_child_by_pid(current, pid);
    if (!p || is_thread(p))
        return -ECHILD;
    if (p->ppid != current->pid)
        return -ECHILD;
    ret = wait_for(p, &status_val, options & WNOHANG, options & WUNTRACED);
    if (ret > 0 && status)
        return put_user(status_val, status) ? -EFAULT : ret;
    return ret;
}

/**
 * Queue current on wq (if not already queued) and mark it asleep in the given
 * state
 */
void prepare_wait(struct waitqueue *wq, struct wq_entry *wait, u8 state)
{
    unsigned long flags;

    acquire_lock_irqsave(&wq->lock, &flags);
    if (list_empty(&wait->entry))
        __add_wait_entry(wait, wq);
    release_lock_irqrestore(&wq->lock, flags);
    set_current_state(state);
}

// Call after prepare_wait for a stack-allocated entry
void end_wait(struct waitqueue *wq, struct wq_entry *wait)
{
    set_task_running(current);
    remove_wait_entry(wait, wq);
}

bool wait_signal_pending(void)
{
    return sig_get_active(current) != 0;
}

static void wakeup_by_pid_on(int pid, struct waitqueue *wq)
{
    unsigned long flags;

    acquire_lock_irqsave(&wq->lock, &flags);
    struct wq_entry *wq_ent = find_waiting_task(wq, pid);
    if (wq_ent) {
        struct task *task = wq_ent->task;
        __remove_wait_entry(wq_ent);
        set_task_running(task);
    }
    release_lock_irqrestore(&wq->lock, flags);
}

struct task * wake_first(struct waitqueue *wq)
{
    struct task *task = NULL;
    unsigned long flags;

    acquire_lock_irqsave(&wq->lock, &flags);
    if (!list_empty(&wq->task_list)) {
        struct wq_entry *wait =
            list_first_entry(&wq->task_list, struct wq_entry, entry);
        task = wait->task;
        __remove_wait_entry(wait);
        set_task_running(task);
    }
    release_lock_irqrestore(&wq->lock, flags);

    return task;
}

// Caller holds wq->lock
void __wake_all(struct waitqueue *wq)
{
    struct wq_entry *wait, *tmp;

    list_for_each_entry_safe(wait, tmp, &wq->task_list, entry) {
        struct task *task = wait->task;
        __remove_wait_entry(wait);
        set_task_running(task);
    }
}

void wake_all(struct waitqueue *wq)
{
    unsigned long flags;
    acquire_lock_irqsave(&wq->lock, &flags);
    __wake_all(wq);
    release_lock_irqrestore(&wq->lock, flags);
}

void wake_parent_waiter(struct task *parent)
{
    wakeup_by_pid_on(parent->pid, &wait_q);
}

void notify_parent(struct task *parent, struct task *child)
{
    if (!parent || !child) {
        klog(LOG_ERROR, "Invalid parent or child task in notify_parent\n");
        return;
    }
    klog(LOG_DEBUG, "Notifying parent %d of child %d exit\n", parent->pid, child->pid);
    // the parent's wait loop rechecks its children, so a
    // spurious wake is harmless and a missed one hangs waitpid
    wakeup_by_pid_on(parent->pid, &wait_q);
    if (child->exit_signal > 0) {
        int st = child->exit_status;
        struct ksiginfo info = {
            .code = (st & 0x7f) ? CLD_KILLED : CLD_EXITED,
            .pid = child->pid,
            .status = (st & 0x7f) ? (st & 0x7f) : (st >> 8) & 0xff,
        };
        do_raise_info(parent, child->exit_signal, &info);
    }
}
