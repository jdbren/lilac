#include <stdbool.h>
#include <kernel/mutex.h>
#include <kernel/process.h>
#include <kernel/sched.h>

void mutex_init(mutex_t *mutex)
{
    atomic_init(&mutex->owner, -1);
    list_init(&mutex->waiters);
    atomic_init(&mutex->locked, false);
}

void mutex_lock(mutex_t *mutex)
{
    int id = get_pid();
    if (atomic_load(&mutex->owner) == id)
        return;
    while (atomic_exchange(&mutex->locked, true))
    {
        list_add(&mutex->waiters, id);
        yield();
    }
    atomic_store(&mutex->owner, id);
}

void mutex_unlock(mutex_t *mutex)
{
    int id = get_pid();
    if (atomic_load(&mutex->owner) != id)
        return;
    atomic_store(&mutex->owner, -1);
    atomic_store(&mutex->locked, false);
    if (!list_empty(&mutex->waiters))
    {
        int next = list_pop(&mutex->waiters);
        atomic_store(&mutex->owner, next);
    }
}

void mutex_destroy(mutex_t *mutex)
{
    list_destroy(&mutex->waiters);
    atomic_store(&mutex->locked, false);
    atomic_store(&mutex->owner, -1);
}
