#ifndef _LILAC_TIMER_H
#define _LILAC_TIMER_H

#include <stdatomic.h>
#include <lilac/types.h>
#include <lilac/math.h>
#include <lilac/time.h>

#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID  3
#define CLOCK_MONOTONIC_RAW      4
#define CLOCK_REALTIME_COARSE    5
#define CLOCK_MONOTONIC_COARSE   6
#define CLOCK_BOOTTIME           7

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
    u64 start_tick;
};

extern struct clock_source *__system_clock;
extern u64 ticks_per_ms;
extern ktime_t system_time_base_ns;

void timer_init(void);
void timer_tick_init(void); // arch
void timer_tick(bool user);

void busy_wait_usec(u32 micros);
void usleep(u32 micros);
ktime_t get_sys_time_ns(void);
time_t get_unix_time(void);
struct timestamp get_timestamp(void);
struct timestamp unix_to_timestamp(time_t unix_time);

#define ktime_get() get_sys_time_ns()

extern time_t boot_unix_time;
extern void (*handle_tick)(unsigned long ms);

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

static inline u64 clock_ticks_to_ns(struct clock_source *cs, u64 t)
{
    u32 mult = READ_ONCE(cs->scale.mult);
    u32 shift = READ_ONCE(cs->scale.shift);
#ifdef __SIZEOF_INT128__
    __uint128_t prod = (__uint128_t) t * mult;
    return (u64)(prod >> shift);
#else
    return mul_u64_u32_shr(t, mult, shift);
#endif
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
    return clock_ticks_to_ns(cs, t);
}

#endif
