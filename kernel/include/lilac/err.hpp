#ifndef _LILAC_ERR_H
#define _LILAC_ERR_H

#include <type_traits>

#include <lilac/types.h>
#include <lilac/config.h>
#include <lilac/errno.h>

#define MAX_ERRNO       4095
#define IS_ERR_VALUE(x) unlikely((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)

template<typename T, typename E>
__must_check
constexpr inline T* ERR_PTR(E error) noexcept
{
    static_assert(std::is_integral_v<E> == true, "");
    return reinterpret_cast<T*>(static_cast<intptr_t>(error));
}

__must_check
static inline long PTR_ERR(const void *ptr)
{
    return reinterpret_cast<long>(ptr);
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

template<typename T, typename E>
__must_check
constexpr inline T* ERR_CAST(E *ptr) noexcept
{
	return reinterpret_cast<T*>(ptr);
}

#endif
