#include <stdatomic.h>
#include <lilac/sync.h>
#include <lilac/types.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/log.h>
#include <mm/kmalloc.h>

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
            __pause();
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
    struct mutex_waiter waiter = {.list = LIST_HEAD_INIT(waiter.list), .t = current};

    acquire_lock(&mutex->wait_lock);
    list_add_tail(&waiter.list, &mutex->waiters);

    for (;;) {
        set_current_state(TASK_UNINTERRUPTIBLE);

        if (atomic_load_explicit(&mutex->owner, memory_order_acquire) == get_pid()) {
            set_current_state(TASK_RUNNING);
            release_lock(&mutex->wait_lock);
            return;
        }

        release_lock(&mutex->wait_lock);
        schedule();
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
#ifdef DEBUG
    long id = get_pid();
    if (atomic_load(&mutex->owner) == id)
        panic("Mutex lock by owner (pid %ld)\n", id);
#endif
    if (unlikely(!__mutex_lock_fast(mutex)))
        __mutex_lock_slow(mutex);
}

void mutex_unlock(mutex_t *mutex)
{
    long id = get_pid();
#ifdef DEBUG
    if (atomic_load(&mutex->owner) != id)
        panic("unlock by non-owner");
#endif

    acquire_lock(&mutex->wait_lock);

    if (list_empty(&mutex->waiters)) {
        atomic_store_explicit(&mutex->owner, -1, memory_order_release);
        release_lock(&mutex->wait_lock);
        return;
    }

    struct mutex_waiter *w =
        list_first_entry(&mutex->waiters, struct mutex_waiter, list);

    atomic_store_explicit(&mutex->owner, w->t->pid, memory_order_release);
    list_del(&w->list);
    release_lock(&mutex->wait_lock);
    set_task_running(w->t);
}


void mutex_destroy(mutex_t *mutex)
{
#ifdef DEBUG
    if (atomic_load(&mutex->owner) != -1 || !list_empty(&mutex->waiters))
        panic("Destroying mutex in use or with waiters\n");
#endif
}
