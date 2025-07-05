#include <lilac/wait.h>

#include <lilac/lilac.h>
#include <lilac/sched.h>
#include <lilac/syscall.h>

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

long wait_for(struct task *p)
{
    if (p->state != TASK_ZOMBIE) {
        klog(LOG_DEBUG, "Process %d: Waiting for task %d\n", get_pid(), p->pid);
        sleep_task_on(current, &wait_q);
        p->parent_wait = true;
        yield();
    }

    reap_task(p);
    klog(LOG_DEBUG, "Task %d has exited, continuing task %d\n", p->pid, get_pid());
    kfree(p);
    return 0;
}
SYSCALL_DECL1(waitpid, int, pid)
{
    struct task *p = find_child_by_pid(current, pid);
    if (!p)
        return -ECHILD;
    if (p->ppid != current->pid)
        return -ECHILD;
    return wait_for(p);
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
    wakeup_by_pid_on(parent->pid, &wait_q);
}
