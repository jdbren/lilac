#include <lilac/time.h>
#include <lilac/timer.h>
#include <lilac/syscall.h>
#include <lilac/log.h>
#include <lilac/sched.h>
#include <lilac/uaccess.h>

static void nop(unsigned long x) {}

void (*handle_tick)(unsigned long) = nop;
s64 boot_unix_time = 0;
atomic_uint time_seq = 0;
u64 system_time_base_ns = 0;
static spinlock_t clock_write_lock = SPINLOCK_INIT;

void set_clock_source(struct clock_source *clock)
{
    acquire_lock(&clock_write_lock);
    u64 current_ns = get_sys_time_ns();

    unsigned int seq = atomic_load_explicit(&time_seq, memory_order_relaxed);
    atomic_store_explicit(&time_seq, seq + 1, memory_order_relaxed);
    // write barrier
    atomic_thread_fence(memory_order_acq_rel);

    system_time_base_ns = current_ns;
    clock->start_tick = clock->read();

    WRITE_ONCE(ticks_per_ms, clock->freq_hz / 1000);
    WRITE_ONCE(__system_clock, clock);

    atomic_store_explicit(&time_seq, seq + 2, memory_order_release);

    release_lock(&clock_write_lock);

    klog(LOG_INFO, "System clock set to %s (%llu Hz)\n",
         clock->name, clock->freq_hz);
}

void timer_init(void)
{
    timer_tick_init();
    kstatus(STATUS_OK, "System clock initialized\n");
}

void timer_tick(void)
{
    handle_tick(1000);
    sched_tick();
}

s64 get_unix_time(void)
{
    return boot_unix_time + get_sys_time_ns() / 1'000'000'000ull;
}

// Get system timer in 1 ns intervals
u64 get_sys_time_ns(void)
{
    unsigned int seq;
    u64 ns = 0;

    do {
        seq = atomic_load_explicit(&time_seq, memory_order_acquire);
        struct clock_source *cs = READ_ONCE(__system_clock);

        u64 delta = cs->read() - cs->start_tick;
        ns = system_time_base_ns + clock_ticks_to_ns(cs, delta);

    } while (seq & 1 || seq != atomic_load_explicit(&time_seq, memory_order_acquire));

    return ns;
}

SYSCALL_DECL1(time, time_t *, t)
{
    struct task *p = current;
    long long ret = get_unix_time();

    if (t) {
        if (access_ok(t, sizeof(time_t)))
            *t = ret;
        else
            ret = -EFAULT;
    }

    return ret;
}

SYSCALL_DECL2(gettimeofday, struct timeval*, tv, struct timezone*, tz)
{
    if (tv) {
        u64 sys_time = get_sys_time_ns();
        tv->tv_sec = boot_unix_time + sys_time / 1000000000;
        tv->tv_usec = sys_time / 1000 % 1000000;
    }
    if (tz) {
        // Timezone not supported
        tz->tz_minuteswest = 0;
        tz->tz_dsttime = 0;
    }
    return 0;
}

__attribute__((optimize("O0")))
void usleep(u32 micros)
{
    // Get current time
    u64 start = get_sys_time_ns();
    u64 end = start + (u64)micros * 1000;
    // if (end - start > 100000) {
    //     yield();
    // }
    while (get_sys_time_ns() < end) {
        pause();
    }
}

SYSCALL_DECL2(nanosleep, const struct timespec*, duration, struct timespec*, rem)
{
    u64 total_ns = duration->tv_sec * 1'000'000'000ull + duration->tv_nsec;
    usleep(total_ns / 1000);
    return 0;
}



static int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int days_in_month(int month, int year) {
    if (month == 2) {
        return is_leap_year(year) ? 29 : 28;
    }
    int days_in_months[] = { 31, 30, 31, 30, 31, 31, 31, 30, 31, 30, 31, 31 };
    return days_in_months[month - 1]; // month is 1-12
}

static void unix_time_to_date(long long unix_time, struct timestamp *ts) {
    // Start from the epoch time
    s64 remaining_seconds = unix_time;

    // Calculate the year
    ts->year = 1970;
    while (remaining_seconds >= (is_leap_year(ts->year) ? 31622400 : 31536000)) {
        remaining_seconds -= (is_leap_year(ts->year) ? 31622400 : 31536000);
        (ts->year)++;
    }

    // Calculate the month
    ts->month = 1;
    while (remaining_seconds >= days_in_month(ts->month, ts->year) * 24 * 60 * 60) {
        remaining_seconds -= days_in_month(ts->month, ts->year) * 24 * 60 * 60;
        (ts->month)++;
    }

    // Calculate the day
    ts->day = 1;
    while (remaining_seconds >= 24 * 60 * 60) {
        remaining_seconds -= 24 * 60 * 60;
        (ts->day)++;
    }

    // Calculate hours, minutes, and seconds
    ts->hour = remaining_seconds / 3600;
    remaining_seconds %= 3600;
    ts->minute = remaining_seconds / 60;
    ts->second = remaining_seconds % 60;

}

struct timestamp get_timestamp(void)
{
    struct timestamp ts;
    long long unix_time = get_unix_time();
    unix_time_to_date(unix_time, &ts);
    return ts;
}
