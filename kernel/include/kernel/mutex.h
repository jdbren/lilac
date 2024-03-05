#ifndef _KERNEL_MUTEX_H
#define _KERNEL_MUTEX_H

#include <stdatomic.h>
#include <kernel/types.h>
#include <kernel/list.h>

typedef struct mutex {
    atomic_int owner;
    list_t waiters;
    atomic_bool locked;
} mutex_t;

void mutex_init(mutex_t *mutex);
void mutex_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);
void mutex_destroy(mutex_t *mutex);

#endif
