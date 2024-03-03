#include <kernel/sync.h>
#include <kernel/types.h>
#include <mm/kheap.h>

#define MAX_SPINLOCKS 16

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
        spin = kzmalloc(sizeof(spinlock_t));
    return spin;
}

void delete_lock(spinlock_t *spin)
{
    if (push_spin(spin))
        kfree(spin);
}

void acquire_lock(spinlock_t *spin)
{
    asm("cli");
	while(atomic_flag_test_and_set(spin))
	{
		__builtin_ia32_pause();
	}
}

void release_lock(spinlock_t *spin)
{
	atomic_flag_clear(spin);
    asm("sti");
}
