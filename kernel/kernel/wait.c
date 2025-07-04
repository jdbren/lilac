#include <lilac/sched.h>

#include <lilac/lilac.h>
#include <lilac/syscall.h>

struct waitqueue {
    spinlock_t lock;
    struct list_head task_list;
};

struct wq_entry {
    struct task *task;
    //wait_queue_func_t func; // callback function, e.g. try_to_wake_up()
    struct list_head entry;
};

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

static struct wq_entry *find_waiting_task(int pid)
{
    struct wq_entry *wait = NULL;
    list_for_each_entry(wait, &wait_q.task_list, entry) {
        if (wait->task->pid == pid)
            return wait;
    }
    return NULL;
}

void wakeup_task(struct task *p)
{
    if (p->state == TASK_SLEEPING) {
        p->state = TASK_RUNNING;
        rq_add(p);
        klog(LOG_DEBUG, "Waking up task %d\n", p->pid);
    }
}

void wakeup(int pid)
{
    struct wq_entry *wq_ent = find_waiting_task(pid);
    if (!wq_ent)
        return;
    remove_wait_entry(wq_ent, &wait_q);
    wakeup_task(wq_ent->task);
    release_wq_entry(wq_ent);
}

void sleep_task(struct task *p)
{
    struct wq_entry *wait;
    if (p->state == TASK_RUNNING) {
        rq_del(p);
        p->state = TASK_SLEEPING;
        wait = alloc_wq_entry(p, &wait_q);
        if (!wait) {
            klog(LOG_ERROR, "Failed to allocate wait entry for task %d\n", p->pid);
            return;
        }
        add_wait_entry(wait, &wait_q);
        klog(LOG_DEBUG, "Task %d is now sleeping\n", p->pid);
    }
}

long wait_task(struct task *p)
{
    if (p->state != TASK_ZOMBIE) {
        klog(LOG_DEBUG, "Process %d: Waiting for task %d\n", get_pid(), p->pid);
        sleep_task(current);
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
    return wait_task(p);
}
