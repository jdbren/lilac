#ifndef _KERNEL_SYNC_H
#define _KERNEL_SYNC_H

#include <stdatomic.h>

typedef atomic_flag spinlock_t;

#define SPINLOCK_INIT ATOMIC_FLAG_INIT

spinlock_t *create_lock();
void delete_lock(spinlock_t *spin);

void acquire_lock(spinlock_t *spin);
void release_lock(spinlock_t *spin);

#endif
