#ifndef _KERNEL_SYNC_H
#define _KERNEL_SYNC_H

#include <stdatomic.h>
#include <lilac/types.h>
#include <lilac/list.h>

typedef atomic_flag spinlock_t;
#define SPINLOCK_INIT ATOMIC_FLAG_INIT

#ifdef __x86_64__
#define pause __builtin_ia32_pause
#else
#define pause __builtin_ia32_pause
#endif

static inline void spin_lock_init(spinlock_t *spin)
{
    atomic_flag_clear(spin);
}

static inline void acquire_lock(spinlock_t *spin)
{
    while(atomic_flag_test_and_set(spin))
		pause();
}

static inline void release_lock(spinlock_t *spin)
{
    atomic_flag_clear(spin);
}

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



typedef volatile struct mutex {
    atomic_int owner;
    struct list_head waiters;
    atomic_bool locked;
} mutex_t;

void mutex_init(mutex_t *mutex);
void mutex_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);
void mutex_destroy(mutex_t *mutex);

#define MUTEX_INIT { .owner = -1, .locked = false }

#endif
