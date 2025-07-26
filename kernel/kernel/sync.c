#include <stdatomic.h>
#include <lilac/sync.h>
#include <lilac/types.h>
#include <lilac/process.h>
#include <lilac/sched.h>
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
    atomic_init(&mutex->locked, false);
}

void mutex_lock(mutex_t *mutex)
{
    // bool add = false;
    int id = get_pid();
    if (atomic_load(&mutex->owner) == id)
        return;
    while (atomic_exchange(&mutex->locked, true))
        yield();
    atomic_store(&mutex->owner, id);
}

void mutex_unlock(mutex_t *mutex)
{
    int id = get_pid();
    if (atomic_load(&mutex->owner) != id)
        return;
    atomic_store(&mutex->owner, -1);
    atomic_store(&mutex->locked, false);
    // if (!list_empty(&mutex->waiters))
    // {
    //     int next = list_pop(&mutex->waiters);
    //     atomic_store(&mutex->owner, next);
    // }
}

void mutex_destroy(mutex_t *mutex)
{
    // list_destroy(&mutex->waiters);
    atomic_store(&mutex->locked, false);
    atomic_store(&mutex->owner, -1);
}
