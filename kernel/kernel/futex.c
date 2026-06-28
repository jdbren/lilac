// Copyright (C) 2025 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <stdatomic.h>
#include <lilac/futex.h>
#include <lilac/sched.h>
#include <lilac/sync.h>
#include <lilac/log.h>
#include <lilac/syscall.h>
#include <lilac/uaccess.h>
#include <lilac/errno.h>
#include <lilac/timer.h>
#include <lilac/timer_event.h>
#include <lib/list.h>
#include <mm/kmalloc.h>

#define FUTEX_WAIT          0
#define FUTEX_WAKE          1
#define FUTEX_PRIVATE       128

#define FUTEX_CMD_MASK      (~(FUTEX_PRIVATE))

#define FUTEX_HASH_SIZE     256
#define FUTEX_HASH_MASK     (FUTEX_HASH_SIZE - 1)

struct futex_waiter {
    uintptr_t       uaddr;
    struct task    *task;
    struct wq_entry wq;
};

struct futex_bucket {
    spinlock_t       lock;
    struct list_head waiters;
};

static struct futex_bucket futex_table[FUTEX_HASH_SIZE];

void futex_init(void)
{
    for (int i = 0; i < FUTEX_HASH_SIZE; i++) {
        spin_lock_init(&futex_table[i].lock);
        INIT_LIST_HEAD(&futex_table[i].waiters);
    }
}

static struct futex_bucket *futex_hash(uintptr_t uaddr)
{
    uintptr_t key = uaddr >> 2;
    return &futex_table[key & FUTEX_HASH_MASK];
}


// returns 1 if value at uaddr matches val, 0 if not or error
int futex_check_value_locked(int __user *uaddr, int val)
{
    int uval = 0;
    int ret = get_user(uval, uaddr);
    return ret < 0 ? ret : (uval == val);
}

int futex_wait(int __user *uaddr, int val, ktime_t abs_to)
{
    struct timer_event timeout_ev;
    struct futex_bucket *bucket;
    struct futex_waiter waiter;
    int ret = 0;

    if (abs_to) {
        timeout_ev = TIMER_EV_INIT(timeout_ev, current, abs_to, NULL, NULL);
        assert(RB_EMPTY_NODE(&timeout_ev.node));
        timer_ev_enqueue(&timeout_ev, current);
    }


    waiter.uaddr = (uintptr_t)uaddr;
    waiter.task = current;
    INIT_LIST_HEAD(&waiter.wq.entry);

    bucket = futex_hash((uintptr_t)uaddr);

    acquire_lock(&bucket->lock);

    // check if the value at uaddr still matches the expected value
    // we hold the hash bucket lock so we can't lose a wakeup
    int matches = futex_check_value_locked(uaddr, val);
    if (matches <= 0) {
        release_lock(&bucket->lock);
        return matches == 0 ? -EAGAIN : matches;
    }

    set_task_sleeping(current);
    list_add_tail(&waiter.wq.entry, &bucket->waiters);

    release_lock(&bucket->lock);

    // if we were already woken up we don't need to sleep
    if (!WQ_ENTRY_EMPTY(waiter.wq)) {
        if (!abs_to || timer_ev_queued(&timeout_ev))
            schedule();
    }
    __set_current_state(TASK_RUNNING);

    acquire_lock(&bucket->lock);
    if (!WQ_ENTRY_EMPTY(waiter.wq))
        list_del_init(&waiter.wq.entry);

    if (abs_to) {
        if (timer_ev_queued(&timeout_ev)) {
            // we were woken up, remove the timeout if it hasn't expired yet
            timer_ev_dequeue(&timeout_ev);
        } else {
            // we timed out
            ret = -ETIMEDOUT;
        }
    }
    release_lock(&bucket->lock);

    if (task_interrupted_ack())
        ret = -EINTR;

    return ret;
}

int futex_wake(int __user *uaddr, int count)
{
    struct futex_bucket *bucket;
    struct futex_waiter *waiter, *tmp;
    int woken = 0;

    if (count <= 0)
        return 0;

    bucket = futex_hash((uintptr_t)uaddr);

    acquire_lock(&bucket->lock);

    list_for_each_entry_safe(waiter, tmp, &bucket->waiters, wq.entry) {
        if (waiter->uaddr != (uintptr_t)uaddr)
            continue;
        list_del_init(&waiter->wq.entry);
        set_task_running(waiter->task);
        if (++woken >= count)
            break;
    }

    release_lock(&bucket->lock);

    return woken;
}

SYSCALL_DECL6(futex, int __user *, uaddr, int, op, int, val,
    struct timespec __user *, timeout, int __user *, uaddr2, int, val3)
{
    int cmd = op & FUTEX_CMD_MASK;
    struct timespec to;

    if (!uaddr || ((uintptr_t)uaddr & 3))
        return -EINVAL;

    if (!access_ok(uaddr, sizeof(int)))
        return -EFAULT;

    klog(LOG_DEBUG, "valid sys_futex(): cmd=%d, uaddr=%p, val=%d\n",
        cmd, uaddr, val);

    switch (cmd) {
    case FUTEX_WAIT:
        if (timeout && copy_from_user(&to, timeout, sizeof(to)))
            return -EFAULT;
        ktime_t abs_to = 0;
        if (timeout) {
            abs_to = ktime_add_safe(ktime_get(), timespec_to_ktime(to));
        }
        return futex_wait(uaddr, val, abs_to);
    case FUTEX_WAKE:
        return futex_wake(uaddr, val);
    default:
        klog(LOG_WARN, "futex: unsupported op %d\n", op);
        return -ENOSYS;
    }
}
