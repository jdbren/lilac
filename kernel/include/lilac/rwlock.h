#ifndef RWLOCK_H
#define RWLOCK_H

#include <lilac/sync.h>

typedef volatile atomic_int rwlock_t;
#define RWLOCK_INIT 0

#define rwlock_init(lock) atomic_store_explicit(lock, RWLOCK_INIT, memory_order_relaxed)

static inline void acquire_read_lock(rwlock_t *lock)
{
    int expected;
    do {
        expected = atomic_load_explicit(lock, memory_order_relaxed);
        if (expected < 0) {
            __pause();
            continue;
        }
    } while (!atomic_compare_exchange_weak_explicit(lock, &expected, expected + 1, memory_order_acquire, memory_order_relaxed));
}

static inline void release_read_lock(rwlock_t *lock)
{
    atomic_fetch_sub_explicit(lock, 1, memory_order_release);
}

static inline void acquire_write_lock(rwlock_t *lock) {
    int expected = 0;
    while (!atomic_compare_exchange_weak_explicit(lock, &expected, -1, memory_order_acquire, memory_order_relaxed)) {
        expected = 0;
        __pause();
    }
}

static inline void release_write_lock(rwlock_t *lock)
{
    atomic_store_explicit(lock, 0, memory_order_release);
}

#endif
