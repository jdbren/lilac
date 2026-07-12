#ifndef LILAC_BITMAP_H
#define LILAC_BITMAP_H

#include <lilac/types.h>

#define BITMAP_SIZE(bits) (((bits) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define BITS_TO_LONGS(bits) BITMAP_SIZE(bits)

static inline int __test_bit(const unsigned long *bitmap, unsigned int bit)
{
    return (bitmap[bit / BITS_PER_LONG] >> (bit % BITS_PER_LONG)) & 1UL;
}

static inline void __set_bit(unsigned long *bitmap, unsigned int bit)
{
    bitmap[bit / BITS_PER_LONG] |= (1UL << (bit % BITS_PER_LONG));
}

static inline void __clear_bit(unsigned long *bitmap, unsigned int bit)
{
    bitmap[bit / BITS_PER_LONG] &= ~(1UL << (bit % BITS_PER_LONG));
}

#endif
