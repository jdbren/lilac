#ifndef LILAC_WAIT_H
#define LILAC_WAIT_H

#include <lilac/config.h>
#include <lilac/sync.h>
#include <lilac/errno.h>
#include <lilac/err.h>
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
#define WCOREDUMP(sig) ((sig & 0x7f) | SCORE)


struct waitqueue {
    spinlock_t lock;
    struct list_head task_list;
};

#define WAITQUEUE_INIT(name) { \
    .lock = SPINLOCK_INIT, \
    .task_list = LIST_HEAD_INIT(name.task_list) \
}

typedef int (*wq_wake_func_t)();

struct wq_entry {
    struct task *task;
    wq_wake_func_t wakeup;
    struct list_head entry;
};

#define WQ_ENTRY_INIT(name, t, w) { \
    .task = t, \
    .wakeup = w, \
    .entry = LIST_HEAD_INIT(name.entry) \
}

#define WQ_ENTRY_EMPTY(name) (list_empty(&name.entry))

void prepare_wait(struct waitqueue *wq, struct wq_entry *wait, u8 state);
void end_wait(struct waitqueue *wq, struct wq_entry *wait);
bool wait_signal_pending(void);
struct task * wake_first(struct waitqueue *wq);
void wake_all(struct waitqueue *wq);
void __wake_all(struct waitqueue *wq);

void notify_parent(struct task *parent, struct task *child);
void wake_parent_waiter(struct task *parent);

/**
 * Sleep until cond is true, ignoring signals. Whoever makes cond true must
 * wake wq afterwards (wake_all / wake_first). Safe to wake from IRQ context.
 */
#define wait_event_uninterruptible(wq, cond) do { \
    struct wq_entry __wait = WQ_ENTRY_INIT(__wait, current, NULL); \
    for (;;) { \
        prepare_wait(&(wq), &__wait, TASK_UNINTERRUPTIBLE); \
        if (cond) \
            break; \
        yield(); \
    } \
    end_wait(&(wq), &__wait); \
} while (0)

/**
 * An unblocked signal ends the wait.
 * Evaluates to 0 once cond is true, or -ERESTARTSYS if a signal arrived first.
 */
#define wait_event_interruptible(wq, cond) ({ \
    struct wq_entry __wait = WQ_ENTRY_INIT(__wait, current, NULL); \
    int __ret = 0; \
    for (;;) { \
        prepare_wait(&(wq), &__wait, TASK_SLEEPING); \
        if (cond) \
            break; \
        if (wait_signal_pending()) { \
            __ret = -ERESTARTSYS; \
            break; \
        } \
        yield(); \
    } \
    end_wait(&(wq), &__wait); \
    task_interrupted_ack(); \
    __ret; \
})

#endif // LILAC_WAIT_H
