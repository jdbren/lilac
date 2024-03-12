#include <stdatomic.h>
#include <kernel/sync.h>
#include <kernel/types.h>
#include <kernel/sched.h>
#include <mm/kheap.h>

#define MAX_SPINLOCKS 16
#define pause __builtin_ia32_pause

/*
    Spinlocks
*/

static const spinlock_t clear = SPINLOCK_INIT;
spinlock_t *spinlocks[MAX_SPINLOCKS];
int top = -1;

int push_spin(spinlock_t *s)
{
    if (top == MAX_SPINLOCKS - 1)
        return -1;

    spinlocks[++top] = s;
    return 0;
}

spinlock_t *get_spin()
{
    if (top == -1)
        return NULL;
    return spinlocks[top--];
}

spinlock_t *create_lock()
{
    spinlock_t *spin = get_spin();
    if (!spin)
        spin = kmalloc(sizeof(spinlock_t));
    *spin = clear;
    return spin;
}

void delete_lock(spinlock_t *spin)
{
    if (push_spin(spin))
        kfree(spin);
}

void acquire_lock(spinlock_t *spin)
{
	while(atomic_flag_test_and_set(spin))
	{
		pause();
	}
}

void release_lock(spinlock_t *spin)
{
	atomic_flag_clear(spin);
}

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
