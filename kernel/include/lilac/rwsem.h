#ifndef LILAC_RWSEM_H
#define LILAC_RWSEM_H

#include <lilac/types.h>
#include <lilac/sync.h>
#include <lilac/wait.h>

struct rw_semaphore {
    atomic_long count;
    struct waitqueue wait_list;
};

typedef struct rw_semaphore rwsem_t;

static inline int rwsem_is_locked(struct rw_semaphore *sem)
{
    return atomic_load(&sem->count) != 0;
}

#define RWSEM_UNLOCKED_VALUE        0L

#define __RWSEM_INITIALIZER(name)                           \
    { .count = RWSEM_UNLOCKED_VALUE,                        \
      .wait_list = WAITQUEUE_INIT((name).wait_list) }

#define DECLARE_RWSEM(name) \
    struct rw_semaphore name = __RWSEM_INITIALIZER(name)

static inline int rwsem_is_contended(struct rw_semaphore *sem)
{
    return !list_empty(&sem->wait_list.task_list);
}

void rwsem_init(struct rw_semaphore *sem);

/*
 * lock for reading
 */
void down_read(struct rw_semaphore *sem);

/*
 * trylock for reading -- returns 1 if successful, 0 if contention
 */
int down_read_trylock(struct rw_semaphore *sem);

/*
 * lock for writing
 */
void down_write(struct rw_semaphore *sem);

/*
 * trylock for writing -- returns 1 if successful, 0 if contention
 */
int down_write_trylock(struct rw_semaphore *sem);

/*
 * release a read lock
 */
void up_read(struct rw_semaphore *sem);

/*
 * release a write lock
 */
void up_write(struct rw_semaphore *sem);

/*
 * downgrade write lock to read lock
 */
void downgrade_write(struct rw_semaphore *sem);

#endif
