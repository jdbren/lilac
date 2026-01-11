#include <lilac/time.h>
#include <lilac/timer.h>
#include <lilac/timer_event.h>
#include <lilac/syscall.h>
#include <lilac/log.h>
#include <lilac/sched.h>
#include <lilac/percpu.h>
#include <lilac/uaccess.h>

static void nop(unsigned long x) {}

void (*handle_tick)(unsigned long) = nop;
s64 boot_unix_time = 0;
atomic_uint time_seq = 0;
u64 system_time_base_ns = 0;
static spinlock_t clock_write_lock = SPINLOCK_INIT;

DEFINE_PER_CPU(struct rb_root_cached, timer_event_tree) = RB_ROOT_CACHED;

bool timer_ev_less(struct rb_node *a, const struct rb_node *b)
{
    struct timer_event *eva = rb_entry(a, struct timer_event, node);
    struct timer_event *evb = rb_entry(b, struct timer_event, node);
    return eva->expires < evb->expires;
}

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
    timer_ev_tick();
    sched_tick();
}

s64 get_unix_time(void)
{
    return boot_unix_time + get_sys_time_ns() / NS_PER_SEC;
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


static void timer_ev_enqueue(struct timer_event *ev, struct task *p)
{
    struct rb_root_cached *root = get_cpu_var(timer_event_tree);
    timer_ev_add(ev, root);
    list_add_tail(&ev->task_list, &p->timer_ev_list);
}

static void timer_ev_dequeue(struct timer_event *ev)
{
    struct rb_root_cached *root = get_cpu_var(timer_event_tree);
    timer_ev_del(ev, root);
    list_del(&ev->task_list);
}

void timer_ev_tick(void)
{
    u64 now_ns = get_sys_time_ns();
    struct rb_root_cached *root = get_cpu_var(timer_event_tree);
    struct rb_node *node;

    while ((node = root->rb_leftmost) != NULL) {
        struct timer_event *ev = rb_entry(node, struct timer_event, node);
        if (ev->expires > now_ns)
            break;

        timer_ev_dequeue(ev);
        ev->callback(ev);
    }
}

static void sleep_ev_callback(struct timer_event *ev)
{
    set_task_running((struct task*)ev->context);
}

void busy_wait_usec(u32 micros)
{
    u64 start = get_sys_time_ns();
    u64 end = start + (u64)micros * 1000;
    while (get_sys_time_ns() < end)
        __pause();
}

__attribute__((optimize("O0")))
void usleep(u32 micros)
{
    u64 start = get_sys_time_ns();
    u64 end = start + (u64)micros * 1000;
    if (micros >= 1000) {
        struct timer_event ev;
        ev.expires = end;
        ev.callback = sleep_ev_callback;
        ev.context = (void *)current;

        set_task_sleeping(current);
        timer_ev_enqueue(&ev, current);
        schedule();
    } else {
        while (get_sys_time_ns() < end)
            __pause();
    }
}

SYSCALL_DECL2(nanosleep, const struct timespec*, duration, struct timespec*, rem)
{
    u64 total_ns = duration->tv_sec * NS_PER_SEC + duration->tv_nsec;
    usleep(total_ns / 1000);
    return 0;
}

static void alarm_handler(struct timer_event *ev)
{
    do_raise((struct task *)ev->context, SIGALRM);
    kfree(ev);
}

static unsigned int alarm_cancel(struct task *p, u64 now_ns)
{
    unsigned int sec_remaining = 0;
    struct timer_event *ev, *tmp;
    list_for_each_entry_safe(ev, tmp, &p->timer_ev_list, task_list) {
        if (ev->callback == alarm_handler) {
            sec_remaining = ev->expires > now_ns ?
                (unsigned int)((ev->expires - now_ns) / NS_PER_SEC) : 0;
            timer_ev_dequeue(ev);
            kfree(ev);
            break;
        }
    }
    return sec_remaining;
}

SYSCALL_DECL1(alarm, unsigned int, seconds)
{
    struct task *p = current;
    struct timer_event *ev;
    u64 now = get_sys_time_ns();
    u64 exp = now + (u64)seconds * NS_PER_SEC;

    klog(LOG_DEBUG, "Process %d set alarm for %u seconds\n", p->pid, seconds);

    unsigned int rem = alarm_cancel(p, now);
    if (!seconds)
        return rem;

    ev = kmalloc(sizeof(*ev));
    if (!ev)
        return -ENOMEM;
    ev->expires = exp;
    ev->callback = alarm_handler;
    ev->context = p;

    timer_ev_enqueue(ev, p);
    return rem;
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
