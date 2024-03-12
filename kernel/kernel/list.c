#include <kernel/list.h>
#include <stdbool.h>
#include <mm/kheap.h>

void list_init(list_t *list)
{
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

void list_destroy(list_t *list)
{
    struct node *curr = list->head;
    while (curr) {
        struct node *next = curr->next;
        kfree(curr);
        curr = next;
    }
    list->head = NULL;
    list->count = 0;
}

void list_add(list_t *list, u32 data)
{
    struct node *new = kmalloc(sizeof(struct node));
    new->data = data;
    new->next = NULL;
    if (list->tail) {
        list->tail->next = new;
        list->tail = new;
    }
    else {
        list->head = new;
        list->tail = new;
    }
    list->count++;
}

u32 list_pop(list_t *list)
{
    if (!list->head)
        return 0;
    struct node *head = list->head;
    u32 data = head->data;
    list->head = head->next;
    if (!list->head)
        list->tail = NULL;
    kfree(head);
    list->count--;
    return data;
}

void list_remove(list_t *list,u32 data)
{
    struct node *curr = list->head;
    struct node *prev = NULL;
    while (curr) {
        if (curr->data == data) {
            if (prev)
                prev->next = curr->next;
            else
                list->head = curr->next;
            if (curr == list->tail)
                list->tail = prev;
            kfree(curr);
            list->count--;
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

bool list_empty(list_t *list)
{
    return list->count == 0;
}
