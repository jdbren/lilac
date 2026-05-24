#include <lilac/rwsem.h>
#include <lilac/sched.h>
#include <lilac/wait.h>

void rwsem_init(struct rw_semaphore *sem)
{
    atomic_store(&sem->count, RWSEM_UNLOCKED_VALUE);
    spin_lock_init(&sem->wait_list.lock);
    INIT_LIST_HEAD(&sem->wait_list.task_list);
}

void down_read(struct rw_semaphore *sem)
{
    while (1) {
        long count = atomic_load(&sem->count);
        if (count >= 0) {
            if (atomic_compare_exchange_weak(&sem->count, &count, count + 1))
                return;
        } else {
            sleep_on(&sem->wait_list);
        }
    }
}

int down_read_trylock(struct rw_semaphore *sem)
{
    long count = atomic_load(&sem->count);
    if (count >= 0) {
        if (atomic_compare_exchange_strong(&sem->count, &count, count + 1))
            return 1;
    }
    return 0;
}

void down_write(struct rw_semaphore *sem)
{
    while (1) {
        long count = atomic_load(&sem->count);
        if (count == 0) {
            if (atomic_compare_exchange_weak(&sem->count, &count, -1))
                return;
        } else {
            sleep_on(&sem->wait_list);
        }
    }
}

int down_write_trylock(struct rw_semaphore *sem)
{
    long count = atomic_load(&sem->count);
    if (count == 0) {
        if (atomic_compare_exchange_strong(&sem->count, &count, -1))
            return 1;
    }
    return 0;
}

void up_read(struct rw_semaphore *sem)
{
    long count = atomic_fetch_sub(&sem->count, 1);
    if (count == 1) {
        wake_all(&sem->wait_list);
    }
}

void up_write(struct rw_semaphore *sem)
{
    int count = atomic_fetch_add(&sem->count, 1);
    if (count == -1) {
        wake_all(&sem->wait_list);
    }
}

void downgrade_write(struct rw_semaphore *sem)
{
    atomic_store(&sem->count, 1);
    wake_all(&sem->wait_list);
}
