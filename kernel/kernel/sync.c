#include <stdatomic.h>
#include <lilac/sync.h>
#include <lilac/types.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/log.h>
#include <lilac/kmalloc.h>

/*
    Semaphores
*/

void sem_init(sem_t *sem, int count)
{
    atomic_init(&sem->count, count);
}

void sem_wait(sem_t *sem)
{
    while (1) {
        while (likely(atomic_load_explicit(&sem->count, memory_order_acquire) < 1))
            pause();
        int tmp = atomic_fetch_add_explicit(&sem->count, -1, memory_order_acq_rel);
        if (likely(tmp >= 1))
            break;
        else
            atomic_fetch_add_explicit(&sem->count, 1, memory_order_release);
    }
}

// TODO: Implement timeout
void sem_wait_timeout(sem_t *sem, int timeout)
{
    sem_wait(sem);
}

void sem_post(sem_t *sem)
{
    atomic_fetch_add_explicit(&sem->count, 1, memory_order_release);
}


/*
    Mutexes
*/

void mutex_init(mutex_t *mutex)
{
    atomic_init(&mutex->owner, -1);
    spin_lock_init(&mutex->wait_lock);
    INIT_LIST_HEAD(&mutex->waiters);
}

static void __mutex_lock_slow(mutex_t *mutex)
{
    long id = get_pid();
    struct mutex_waiter waiter = {.t = current};

    acquire_lock(&mutex->wait_lock);
    list_add_tail(&waiter.list, &mutex->waiters);

    for (;;) {
        long expected = -1;
        set_current_state(TASK_UNINTERRUPTIBLE);

        if (atomic_compare_exchange_strong_explicit(&mutex->owner, &expected,
                id, memory_order_acquire, memory_order_relaxed)) {
            list_del(&waiter.list);
            set_current_state(TASK_RUNNING);
            release_lock(&mutex->wait_lock);
            return;
        }

        release_lock(&mutex->wait_lock);
        yield();
        acquire_lock(&mutex->wait_lock);
    }
}

static inline int __mutex_lock_fast(mutex_t *mutex)
{
    long expected = -1;
    return atomic_compare_exchange_strong_explicit(&mutex->owner, &expected,
        get_pid(), memory_order_acquire, memory_order_relaxed);
}

void mutex_lock(mutex_t *mutex)
{
    if (unlikely(!__mutex_lock_fast(mutex)))
        __mutex_lock_slow(mutex);
}

void mutex_unlock(mutex_t *mutex)
{
    long id = get_pid();
    if (atomic_load(&mutex->owner) != id) {
        klog(LOG_ERROR, "Mutex unlock by non-owner (pid %ld)\n", id);
        return;
    }

    atomic_store_explicit(&mutex->owner, -1, memory_order_release);

    acquire_lock(&mutex->wait_lock);
    if (!list_empty(&mutex->waiters)) {
        struct task *next =
            list_first_entry(&mutex->waiters, struct mutex_waiter, list)->t;
        set_task_running(next);
    }
    release_lock(&mutex->wait_lock);
}

void mutex_destroy(mutex_t *mutex)
{
}
