#include <lilac/wait.h>

#include <lilac/lilac.h>
#include <lilac/sched.h>
#include <lilac/syscall.h>

#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"

static struct waitqueue wait_q = {
    .lock = SPINLOCK_INIT,
    .task_list = LIST_HEAD_INIT(wait_q.task_list),
};

static inline
struct wq_entry * alloc_wq_entry(struct task *p, struct waitqueue *wq)
{
    struct wq_entry *wait = kmalloc(sizeof(*wait));
    if (!wait) {
        klog(LOG_ERROR, "Failed to allocate wait queue entry\n");
        return NULL;
    }
    wait->task = p;
    return wait;
}

static inline void release_wq_entry(struct wq_entry *wait)
{
    kfree(wait);
}

static void add_wait_entry(struct wq_entry *wait, struct waitqueue *wq)
{
    acquire_lock(&wq->lock);
    list_add_tail(&wait->entry, &wq->task_list);
    release_lock(&wq->lock);
}

static void remove_wait_entry(struct wq_entry *wait, struct waitqueue *wq)
{
    acquire_lock(&wq->lock);
    list_del(&wait->entry);
    release_lock(&wq->lock);
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


static void sleep_task_on(struct task *p, struct waitqueue *wq)
{
    struct wq_entry *wait;
    set_task_sleeping(p);
    wait = alloc_wq_entry(p, wq);
    if (!wait) {
        klog(LOG_ERROR, "Failed to allocate wait entry for task %d\n", p->pid);
        return;
    }
    add_wait_entry(wait, wq);
}

static pid_t wait_for(struct task *p, int *status, bool nohang)
{
    if (p->state != TASK_ZOMBIE) {
        if (nohang) {
            klog(LOG_DEBUG, "Task %d has not exited yet, returning immediately\n", p->pid);
            return 0;
        }
        klog(LOG_DEBUG, "Process %d: Waiting for task %d\n", get_pid(), p->pid);
        sleep_task_on(current, &wait_q);
        p->parent_wait = true;
        yield();
    }

    pid_t pid = p->pid;
    if (status)
        *status = p->exit_status;
    reap_task(p);
    klog(LOG_DEBUG, "Task %d has exited, continuing task %d\n", p->pid, get_pid());
    kfree(p);
    return 0;
}

static struct task * find_exited_child(struct task *parent, int pid)
{
    struct task *child;
    list_for_each_entry(child, &parent->children, sibling) {
        if (child->state == TASK_ZOMBIE && (pid == WAIT_ANY || child->pid == pid)) {
            return child;
        }
    }
    return NULL;
}

static pid_t wait_any(int *status, bool nohang)
{
    if (list_empty(&current->children)) {
        klog(LOG_DEBUG, "Process %d has no children to wait for\n", current->pid);
        return -ECHILD;
    }
    struct task *child = find_exited_child(current, WAIT_ANY);
    if (child) {
        pid_t child_pid = child->pid;
        if (status) {
            *status = child->exit_status;
            klog(LOG_DEBUG, "wait_any: Child %d exited with status %d\n", child_pid, *status);
        }
        reap_task(child);
        kfree(child);
        return child_pid;
    } else if (nohang) {
        return 0;
    }

    // No child has exited yet, so wait for one
    current->waiting_any = true;
    sleep_task_on(current, &wait_q);
    yield();

    // After being woken up, check for exited children again
    child = find_exited_child(current, WAIT_ANY);
    if (child) {
        pid_t child_pid = child->pid;
        if (status)
            *status = child->exit_status;
        reap_task(child);
        kfree(child);
        return child_pid;
    }

    return -ECHILD; // no children at all
}

SYSCALL_DECL3(waitpid, int, pid, int*, status, int, options)
{
    if (pid < -1 || pid == 0)
        return -EINVAL;
    if (status && !access_ok(status, sizeof(int)))
        return -EFAULT;
    if (pid == -1) {
        return wait_any(status, options & WNOHANG);
    }
    struct task *p = find_child_by_pid(current, pid);
    if (!p)
        return -ECHILD;
    if (p->ppid != current->pid)
        return -ECHILD;
    return wait_for(p, status, options & WNOHANG);
}

void sleep_on(struct waitqueue *wq)
{
    sleep_task_on(current, wq);
    yield();
}

static void wakeup_by_pid_on(int pid, struct waitqueue *wq)
{
    struct wq_entry *wq_ent = find_waiting_task(wq, pid);
    if (!wq_ent)
        return;
    remove_wait_entry(wq_ent, wq);
    set_task_running(wq_ent->task);
    release_wq_entry(wq_ent);
}

struct task * wake_first(struct waitqueue *wq)
{
    struct wq_entry *wait = NULL;
    struct task *task = NULL;

    acquire_lock(&wq->lock);
    if (!list_empty(&wq->task_list)) {
        wait = list_first_entry(&wq->task_list, struct wq_entry, entry);
        list_del(&wait->entry);
        task = wait->task;
        release_wq_entry(wait);
    }
    release_lock(&wq->lock);

    if (task) {
        set_task_running(task);
    }
    return task;
}

void notify_parent(struct task *parent, struct task *child)
{
    if (!parent || !child) {
        klog(LOG_ERROR, "Invalid parent or child task in notify_parent\n");
        return;
    }
    klog(LOG_DEBUG, "Notifying parent %d of child %d exit\n", parent->pid, child->pid);
    parent->waiting_any = false;
    wakeup_by_pid_on(parent->pid, &wait_q);
}
