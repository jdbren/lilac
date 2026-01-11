#ifndef LILAC_TIMER_EVENT_H
#define LILAC_TIMER_EVENT_H

#include <lilac/types.h>
#include <lib/rbtree.h>

struct timer_event {
    struct rb_node node;
    u64 expires;
    void (*callback)(struct timer_event *);
    void *context;
    struct list_head task_list;
};

bool timer_ev_less(struct rb_node *a, const struct rb_node *b);

static inline struct timer_event *
timer_ev_add(struct timer_event *ev, struct rb_root_cached *tree)
{
    struct rb_node *ret = rb_add_cached(&ev->node, tree, timer_ev_less);
    return ret ? ev : NULL;
}

static inline struct timer_event *
timer_ev_del(struct timer_event *ev, struct rb_root_cached *tree)
{
    struct rb_node *ret = rb_erase_cached(&ev->node, tree);
    RB_CLEAR_NODE(&ev->node);
    return ret ? ev : NULL;
}

void timer_ev_tick(void);

#endif
