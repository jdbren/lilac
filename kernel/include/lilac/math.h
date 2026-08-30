#ifndef __LILAC_MATH_H
#define __LILAC_MATH_H

#include <lilac/types.h>
#include <lilac/compiler.h>
#include <lilac/config.h>

__maybe_unused
static inline unsigned long next_pow_2(unsigned long x)
{
    if (x <= 1) return 1;
    return 1UL << (BITS_PER_LONG - __builtin_clzl(x - 1));
}

static inline __attribute__((const))
bool is_pow_2(unsigned long x)
{
    return (x != 0) && ((x & (x - 1)) == 0);
}

static __always_inline u32
__iter_div_u64_rem(u64 dividend, u32 divisor, u64 *remainder)
{
    u32 ret = 0;
    ret = dividend / divisor;
    *remainder = dividend - (u64)ret * divisor;
    return ret;
}

static inline u64 mul_u64_u32_shr(u64 a, u32 mul, unsigned int shift)
{
    u32 ah, al;
    u64 ret;

    al = a;
    ah = a >> 32;

    ret = ((u64)al * mul) >> shift;
    if (ah)
        ret += ((u64)ah * mul) << (32 - shift);

    return ret;
}

#endif
