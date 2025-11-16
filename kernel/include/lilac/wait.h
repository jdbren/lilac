#ifndef LILAC_WAIT_H
#define LILAC_WAIT_H

#include <lilac/config.h>
#include <lilac/sync.h>
#include <lib/list.h>

#define WNOHANG 1
#define WUNTRACED 2

#define WAIT_ANY -1
#define WAIT_PGRP 0

#define SEXITED     0x00
#define SSIGNALED   0x01
#define SSTOPPED    0x7f
#define SCORE       0x80

#define WEXITED(exitval) (exitval << 8)
#define WSIGNALED(sig) (sig & 0x7f)
#define WSTOPPED(sig) (SSTOPPED | ((sig & 0x7f) << 8))


struct waitqueue {
    spinlock_t lock;
    struct list_head task_list;
};

typedef int (*wq_wake_func_t)();

struct wq_entry {
    struct task *task;
    wq_wake_func_t wakeup;
    struct list_head entry;
};

int sleep_on(struct waitqueue *wq);
struct task * wake_first(struct waitqueue *wq);

void notify_parent(struct task *parent, struct task *child);

#endif // LILAC_WAIT_H
