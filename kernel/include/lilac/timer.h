#ifndef _LILAC_TIMER_H
#define _LILAC_TIMER_H

#include <stdatomic.h>
#include <lilac/types.h>
#include <lilac/math.h>
#include <lilac/time.h>

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

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
void timer_tick(void);

void busy_wait_usec(u32 micros);
void usleep(u32 micros);
ktime_t get_sys_time_ns(void);
time_t get_unix_time(void);
struct timestamp get_timestamp(void);

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



/**
 * ktime_set - Set a ktime_t variable from a seconds/nanoseconds value
 * @secs:    seconds to set
 * @nsecs:    nanoseconds to set
 *
 * Return: The ktime_t representation of the value.
 */
static inline ktime_t ktime_set(const s64 secs, const unsigned long nsecs)
{
    if (unlikely(secs >= KTIME_SEC_MAX))
        return KTIME_MAX;

    return secs * NSEC_PER_SEC + (s64)nsecs;
}

/* Subtract two ktime_t variables. rem = lhs -rhs: */
#define ktime_sub(lhs, rhs) ((lhs) - (rhs))

/* Add two ktime_t variables. res = lhs + rhs: */
#define ktime_add(lhs, rhs) ((lhs) + (rhs))

/*
 * Same as ktime_add(), but avoids undefined behaviour on overflow; however,
 * this means that you must check the result for overflow yourself.
 */
#define ktime_add_unsafe(lhs, rhs) ((u64) (lhs) + (rhs))

/*
 * Add a ktime_t variable and a scalar nanosecond value.
 * res = kt + nsval:
 */
#define ktime_add_ns(kt, nsval) ((kt) + (nsval))

/*
 * Subtract a scalar nanosecod from a ktime_t variable
 * res = kt - nsval:
 */
#define ktime_sub_ns(kt, nsval) ((kt) - (nsval))

/* convert a timespec64 to ktime_t format: */
static inline ktime_t timespec_to_ktime(struct timespec ts)
{
    return ktime_set(ts.tv_sec, ts.tv_nsec);
}

/* Map the ktime_t to timespec conversion to ns_to_timespec function */
#define ktime_to_timespec64(kt) ns_to_timespec((kt))

/* Convert ktime_t to nanoseconds */
static inline s64 ktime_to_ns(const ktime_t kt)
{
    return kt;
}

/*
 * Add two ktime values and do a safety check for overflow:
 */
static inline ktime_t ktime_add_safe(const ktime_t lhs, const ktime_t rhs)
{
	ktime_t res = ktime_add_unsafe(lhs, rhs);

	/*
	 * We use KTIME_SEC_MAX here, the maximum timeout which we can
	 * return to user space in a timespec:
	 */
	if (res < 0 || res < lhs || res < rhs)
		res = ktime_set(KTIME_SEC_MAX, 0);

	return res;
}

#endif
