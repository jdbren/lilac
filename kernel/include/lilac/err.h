#ifndef _LILAC_ERR_H
#define _LILAC_ERR_H

#include <lilac/types.h>
#include <lilac/config.h>
#include <lilac/errno.h>

// restart unless a handler without SA_RESTART runs
#define ERESTARTSYS             512
// always restart
#define ERESTARTNOINTR          513
// restart only if no handler runs
#define ERESTARTNOHAND          514
// like ERESTARTNOHAND, resuming from current->restart
#define ERESTART_RESTARTBLOCK   516

#define MAX_ERRNO       4095
#define IS_ERR_VALUE(x) unlikely((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)

__must_check
static inline void * ERR_PTR(long error)
{
    return (void *)error;
}

__must_check
static inline long PTR_ERR(const void *ptr)
{
    return (long)ptr;
}

__must_check
static inline bool IS_ERR(const void *ptr)
{
    return IS_ERR_VALUE((unsigned long)ptr);
}

__must_check
static inline bool IS_ERR_OR_NULL(const void *ptr)
{
    return unlikely(!ptr) || IS_ERR_VALUE((unsigned long)ptr);
}

__must_check
static inline void * ERR_CAST(const void *ptr)
{
	return (void *)ptr;
}

#endif
