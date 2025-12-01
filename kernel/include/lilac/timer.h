#ifndef _LILAC_TIMER_H
#define _LILAC_TIMER_H

#include <lilac/types.h>

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
    return __system_clock->read();
}

static inline u64 clock_freq()
{
    return __system_clock->freq_hz;
}

static inline u64 ms_to_ticks(u64 ms)
{
    return ms * ticks_per_ms;
}

static inline u64 ticks_to_ms(u64 t)
{
    return t / ticks_per_ms;
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
#ifdef __SIZEOF_INT128__
    __uint128_t prod = (__uint128_t) ns * __system_clock->freq_hz;
    return (u64)(prod / 1000000000ull);
#else
    return mul_u64_u32_shr(ns, (u32)(__system_clock->freq_hz / 1000000000ull), 0);
#endif
}

static inline u64 ticks_to_ns(u64 t)
{
#ifdef __SIZEOF_INT128__
    __uint128_t prod = (__uint128_t) t * __system_clock->scale.mult;
    return (u64)(prod >> __system_clock->scale.shift);
#else
    return mul_u64_u32_shr(t, __system_clock->scale.mult, __system_clock->scale.shift);
#endif
}

#endif
