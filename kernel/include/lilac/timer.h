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
extern unsigned long ticks_per_ms;

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

static inline u64 ns_to_ticks(u64 ns)
{
    return (ns * __system_clock->freq_hz) / 1000000000ull;
}

static inline u64 ticks_to_ns(u64 t)
{
    __int128 prod = (__int128) t * (__int128) __system_clock->scale.mult;
    return (u64)(prod >> __system_clock->scale.shift);
}

#endif
