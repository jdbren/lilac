#ifndef LILAC_WAIT_H
#define LILAC_WAIT_H

#include <lilac/sync.h>
#include <lilac/list.h>

struct waitqueue {
    spinlock_t lock;
    struct list_head task_list;
};

struct wq_entry {
    struct task *task;
    //wait_queue_func_t func; // callback function, e.g. try_to_wake_up()
    struct list_head entry;
};

void sleep_on(struct waitqueue *wq);
struct task * wake_first(struct waitqueue *wq);

void notify_parent(struct task *parent, struct task *child);

#endif // LILAC_WAIT_H
