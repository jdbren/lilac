#ifndef LILAC_TIMER_EVENT_H
#define LILAC_TIMER_EVENT_H

#include <lilac/types.h>
#include <lib/rbtree.h>

struct timer_event {
    struct rb_node node;
    ktime_t expires;
    struct task *p;
    void (*callback)(struct timer_event *);
    void *context;
    struct list_head task_list;
};

#define TIMER_EV_INIT(name, t, exp, cb, ctx) (struct timer_event) { \
        .node = RB_CLEAR_NODE(&name.node), \
        .expires = exp, \
        .callback = (cb) ? (cb) : timer_ev_default_callback, \
        .p = t, \
        .context = ctx, \
        .task_list = (struct list_head) LIST_HEAD_INIT(name.task_list) \
    }

void timer_ev_default_callback(struct timer_event *ev);

struct timer_event * create_timer_event(struct task *p,
    ktime_t expires, void (*callback)(struct timer_event *), void *context);
void destroy_timer_event(struct timer_event *ev);

void timer_ev_enqueue(struct timer_event *ev, struct task *p);
void timer_ev_dequeue(struct timer_event *ev);

static inline bool timer_ev_queued(struct timer_event *ev)
{
    return !RB_EMPTY_NODE(&ev->node);
}

#endif
