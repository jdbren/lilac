#ifndef _LILAC_ERR_H
#define _LILAC_ERR_H

#include <lilac/types.h>
#include <lilac/config.h>
#include <lilac/errno.h>

#define MAX_ERRNO       4095
#define IS_ERR_VALUE(x) unlikely((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)

static inline void * __must_check ERR_PTR(long error)
{
    return (void *)error;
}

static inline long __must_check PTR_ERR(const void *ptr)
{
    return (long)ptr;
}

static inline bool __must_check IS_ERR(const void *ptr)
{
    return IS_ERR_VALUE((unsigned long)ptr);
}

static inline bool __must_check IS_ERR_OR_NULL(const void *ptr)
{
    return unlikely(!ptr) || IS_ERR_VALUE((unsigned long)ptr);
}

static inline void * __must_check ERR_CAST(const void *ptr)
{
	return (void *)ptr;
}

#endif
