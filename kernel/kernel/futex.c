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

static int futex_wait(int __user *uaddr, int val)
{
    struct futex_bucket *bucket;
    struct futex_waiter *waiter;
    int cur_val;
    int ret = 0;

    waiter = kmalloc(sizeof(*waiter));
    if (!waiter)
        return -ENOMEM;

    waiter->uaddr = (uintptr_t)uaddr;
    waiter->task = current;
    INIT_LIST_HEAD(&waiter->wq.entry);

    bucket = futex_hash((uintptr_t)uaddr);

    acquire_lock(&bucket->lock);

    if (get_user(cur_val, uaddr)) {
        release_lock(&bucket->lock);
        kfree(waiter);
        return -EFAULT;
    }

    if (cur_val != val) {
        release_lock(&bucket->lock);
        kfree(waiter);
        return -EAGAIN;
    }

    set_task_sleeping(current);
    list_add_tail(&waiter->wq.entry, &bucket->waiters);

    release_lock(&bucket->lock);

    yield();

    kfree(waiter);

    if (task_interrupted_ack())
        ret = -EINTR;

    return ret;
}

static int futex_wake(int __user *uaddr, int count)
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

    if (!uaddr || ((uintptr_t)uaddr & 3))
        return -EINVAL;

    if (!access_ok(uaddr, sizeof(int)))
        return -EFAULT;

    switch (cmd) {
    case FUTEX_WAIT:
        return futex_wait(uaddr, val);
    case FUTEX_WAKE:
        return futex_wake(uaddr, val);
    default:
        klog(LOG_WARN, "futex: unsupported op %d\n", op);
        return -ENOSYS;
    }
}
