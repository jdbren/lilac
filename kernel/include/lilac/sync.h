#ifndef _KERNEL_SYNC_H
#define _KERNEL_SYNC_H

#include <stdatomic.h>
#include <lilac/types.h>

#ifndef __cplusplus
#include <lilac/list.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef atomic_flag spinlock_t;
#define SPINLOCK_INIT ATOMIC_FLAG_INIT

#ifdef __x86_64__
#define pause __builtin_ia32_pause
#else
#define pause __builtin_ia32_pause
#endif

#define spin_lock_init(spin) atomic_flag_clear(spin);

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

#ifndef __cplusplus
struct mutex_waiter {
    struct list_head list;
    struct task *t;
};

typedef struct mutex {
    atomic_long owner;
    struct list_head waiters;
    spinlock_t wait_lock;
} mutex_t;

void mutex_init(mutex_t *mutex);
void mutex_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);
void mutex_destroy(mutex_t *mutex);
#endif // !__cplusplus

#ifdef __cplusplus
} // extern "C"
#endif

#endif
