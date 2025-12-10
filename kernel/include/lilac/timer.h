#ifndef _LILAC_TIMER_H
#define _LILAC_TIMER_H

#include <lilac/types.h>
#include <stdatomic.h>

struct timestamp {
    u16 year;
    u8 month;
    u8 day;
    u8 hour;
    u8 minute;
    u8 second;
};

struct clock_source {
    const char *name;
    u64 (*read)(void);
    u64 freq_hz;
    struct {
        u32 mult;   // fixed-point scale factor
        u32 shift;
    } scale;
};

extern struct clock_source *__system_clock;
extern u64 ticks_per_ms;

void timer_init(void);
void timer_tick_init(void); // arch
void timer_tick(void);

void usleep(u32 micros);
u64 get_sys_time_ns(void);
s64 get_unix_time(void);
struct timestamp get_timestamp(void);

#define TIME_FORMAT "%04u/%02u/%02u %02u:%02u:%02u"

extern s64 boot_unix_time;
extern void (*handle_tick)(unsigned long);

void set_clock_source(struct clock_source *clock);

static inline u64 read_ticks(void)
{
    return atomic_load_explicit(&__system_clock, memory_order_acquire)->read();
}

static inline u64 clock_freq()
{
    return atomic_load_explicit(&__system_clock, memory_order_acquire)->freq_hz;
}

static inline u64 ms_to_ticks(u64 ms)
{
    u64 tpm = READ_ONCE(ticks_per_ms);
    return ms * tpm;
}

static inline u64 ticks_to_ms(u64 t)
{
    u64 tpm = READ_ONCE(ticks_per_ms);
    return t / tpm;
}

static inline
u64 mul_u64_u32_shr(u64 a, u32 mul, unsigned int shift)
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


static inline u64 ns_to_ticks(u64 ns)
{
    struct clock_source *cs = READ_ONCE(__system_clock);
    u64 freq = READ_ONCE(cs->freq_hz);
#ifdef __SIZEOF_INT128__
    __uint128_t prod = (__uint128_t) ns * freq;
    return (u64)(prod / 1000000000ull);
#else
    return mul_u64_u32_shr(ns, (u32)(freq / 1000000000ull), 0);
#endif
}

static inline u64 ticks_to_ns(u64 t)
{
    struct clock_source *cs = READ_ONCE(__system_clock);
    u32 mult = READ_ONCE(cs->scale.mult);
    u32 shift = READ_ONCE(cs->scale.shift);
#ifdef __SIZEOF_INT128__
    __uint128_t prod = (__uint128_t) t * mult;
    return (u64)(prod >> shift);
#else
    return mul_u64_u32_shr(t, mult, shift);
#endif
}

#endif
