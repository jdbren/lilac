#include <lilac/rwlock.h>

void acquire_read_lock_irqsave(rwlock_t *lock, unsigned long *flagp)
{
    int expected;
    arch_local_irq_save(flagp);
    arch_disable_interrupts();
    do {
        expected = atomic_load_explicit(lock, memory_order_relaxed);
        if (expected < 0) {
            __pause();
            continue;
        }
    } while (!atomic_compare_exchange_weak_explicit(lock, &expected, expected + 1, memory_order_acquire, memory_order_relaxed));
}

void release_read_lock_irqrestore(rwlock_t *lock, unsigned long flags)
{
    atomic_fetch_sub_explicit(lock, 1, memory_order_release);
    arch_local_irq_restore(flags);
}

void acquire_write_lock_irqsave(rwlock_t *lock, unsigned long *flagp) {
    int expected = 0;
    arch_local_irq_save(flagp);
    arch_disable_interrupts();
    while (!atomic_compare_exchange_weak_explicit(lock, &expected, -1, memory_order_acquire, memory_order_relaxed)) {
        expected = 0;
        __pause();
    }
}

void release_write_lock_irqrestore(rwlock_t *lock, unsigned long flags)
{
    atomic_store_explicit(lock, 0, memory_order_release);
    arch_local_irq_restore(flags);
}
