#ifndef __LILAC_MATH_H
#define __LILAC_MATH_H

#include <lilac/types.h>

static inline u64 next_pow_2(u64 x)
{
    return 1ULL << (64 - __builtin_clzl(x - 1));
}

static bool is_pow_2(u64 x)
{
    return (x != 0) && ((x & (x - 1)) == 0);
}

#endif
