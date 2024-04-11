#ifndef _KERNEL_SYNC_H
#define _KERNEL_SYNC_H

#include <stdatomic.h>
#include <lilac/types.h>
#include <lilac/list.h>

typedef atomic_flag spinlock_t;
#define SPINLOCK_INIT ATOMIC_FLAG_INIT

spinlock_t *create_lock();
void delete_lock(spinlock_t *spin);
void acquire_lock(spinlock_t *spin);
void release_lock(spinlock_t *spin);

struct lockref {
    spinlock_t lock;
    int count;
};

typedef struct semaphore {
    atomic_int count;
} sem_t;

void sem_init(sem_t *sem, int count);
void sem_wait(sem_t *sem);
void sem_wait_timeout(sem_t *sem, int timeout);
void sem_post(sem_t *sem);



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
