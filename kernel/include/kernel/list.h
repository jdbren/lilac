#ifndef _KERNEL_LIST_H
#define _KERNEL_LIST_H

#include <stdbool.h>
#include <kernel/types.h>

struct node {
    struct node *next;
    u32 data;
};

typedef struct list {
    struct node *head;
    struct node *tail;
    u32 count;
} list_t;

void list_init(list_t *list);
void list_destroy(list_t *list);
void list_add(list_t *list, u32 data);
u32  list_pop(list_t *list);
void list_remove(list_t *list, u32 data);
bool list_empty(list_t *list);

#endif
