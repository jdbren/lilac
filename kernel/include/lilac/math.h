#ifndef __LILAC_MATH_H
#define __LILAC_MATH_H

#include <lilac/types.h>
#include <lilac/compiler.h>
#include <lilac/config.h>

__maybe_unused
static inline u64 next_pow_2(u64 x)
{
    return 1ULL << (64 - __builtin_clzl(x - 1));
}

__maybe_unused
static inline bool is_pow_2(u64 x)
{
    return (x != 0) && ((x & (x - 1)) == 0);
}

static __always_inline u32
__iter_div_u64_rem(u64 dividend, u32 divisor, u64 *remainder)
{
	u32 ret = 0;

	while (dividend >= divisor) {
		/* The following asm() prevents the compiler from
		   optimising this loop into a modulo operation.  */
		asm("" : "+rm"(dividend));

		dividend -= divisor;
		ret++;
	}

	*remainder = dividend;

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
