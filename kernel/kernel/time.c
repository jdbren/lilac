#include <lilac/time.h>
#include <lilac/timer.h>
#include <lilac/timer_event.h>
#include <lilac/syscall.h>
#include <lilac/log.h>
#include <lilac/sched.h>
#include <lilac/percpu.h>
#include <lilac/boot.h>
#include <lilac/uaccess.h>

#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"

static void nop(__unused unsigned long x) {}

void (*handle_tick)(unsigned long ms) = nop;
s64 boot_unix_time = 0;
atomic_uint time_seq = 0;
ktime_t system_time_base_ns = 0;
static spinlock_t clock_write_lock = SPINLOCK_INIT;

struct timer_base {
    struct rb_root_cached tree;
    struct timer_event *running;    // callback in progress on this cpu
    struct task *running_task;      // its ev->p
};

/*
 * Each cpu's timer interrupt only walks its own tree, but events are cancelled
 * from any cpu (the task may have migrated since it queued the event) and the
 * task's timer_ev_list spans cpus
 */
static spinlock_t timer_lock = SPINLOCK_INIT;
static DEFINE_PER_CPU(struct timer_base, timer_bases);

void timer_ev_tick(void);

bool timer_ev_less(struct rb_node *a, const struct rb_node *b)
{
    struct timer_event *eva = rb_entry(a, struct timer_event, node);
    struct timer_event *evb = rb_entry(b, struct timer_event, node);
    return eva->expires < evb->expires;
}

void set_clock_source(struct clock_source *clock)
{
    acquire_lock(&clock_write_lock);
    ktime_t current_ns = ktime_get();

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
    handle_tick(1);
    timer_ev_tick();
    sched_tick();
}

s64 get_unix_time(void)
{
    return boot_unix_time + get_sys_time_ns() / NS_PER_SEC;
}

// Get system timer in 1 ns intervals
ktime_t get_sys_time_ns(void)
{
    unsigned int seq;
    u64 ns;
    ktime_t base;

    do {
        seq = atomic_load_explicit(&time_seq, memory_order_acquire);
        struct clock_source *cs = READ_ONCE(__system_clock);
        base = READ_ONCE(system_time_base_ns);
        ns = clock_ticks_to_ns(cs, cs->read() - cs->start_tick);

    } while (seq & 1 || seq != atomic_load_explicit(&time_seq, memory_order_acquire));

    return ktime_add_ns(base, ns);
}

SYSCALL_DECL1(time, time_t *, t)
{
    long long ret = get_unix_time();

    if (t && put_user((time_t)ret, t))
        return -EFAULT;

    return ret;
}

SYSCALL_DECL2(gettimeofday, struct timeval*, tv, struct timezone*, tz)
{
    if (tv) {
        u64 sys_time = get_sys_time_ns();
        if (put_user((time_t)(boot_unix_time + sys_time / NS_PER_SEC), &tv->tv_sec) ||
            put_user((suseconds_t)(sys_time / 1000 % 1000000), &tv->tv_usec))
            return -EFAULT;
    }
    if (tz) {
        // Timezone not supported
        if (put_user(0, &tz->tz_minuteswest) || put_user(0, &tz->tz_dsttime))
            return -EFAULT;
    }
    return 0;
}

SYSCALL_DECL2(clock_gettime, int, clk, struct timespec*, tp)
{
    u64 ns;
    struct timespec ts;

    switch (clk) {
    case CLOCK_REALTIME:
    case CLOCK_REALTIME_COARSE:
        ns = get_sys_time_ns();
        ts.tv_sec = boot_unix_time + ns / NS_PER_SEC;
        ts.tv_nsec = ns % NS_PER_SEC;
        break;
    case CLOCK_MONOTONIC:
    case CLOCK_MONOTONIC_RAW:
    case CLOCK_MONOTONIC_COARSE:
    case CLOCK_BOOTTIME:
        ns = get_sys_time_ns();
        ts.tv_sec = ns / NS_PER_SEC;
        ts.tv_nsec = ns % NS_PER_SEC;
        break;
    case CLOCK_PROCESS_CPUTIME_ID: // no per-process accounting: thread time
    case CLOCK_THREAD_CPUTIME_ID:
        // runtime is only folded in at scheduler ticks; add the current slice
        ns = current->runtime + ticks_to_ns(read_ticks() - current->exec_started);
        ts.tv_sec = ns / NS_PER_SEC;
        ts.tv_nsec = ns % NS_PER_SEC;
        break;
    default:
        return -EINVAL;
    }
    return copy_to_user(tp, &ts, sizeof(ts)) ? -EFAULT : 0;
}

SYSCALL_DECL2(clock_getres, int, clk, struct timespec*, res)
{
    if (clk < CLOCK_REALTIME || clk > CLOCK_BOOTTIME)
        return -EINVAL;
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 1 };
    if (res && copy_to_user(res, &ts, sizeof(ts)))
        return -EFAULT;
    return 0;
}

static inline struct timer_event *
timer_ev_add(struct timer_event *ev, struct rb_root_cached *tree)
{
    struct rb_node *ret = rb_add_cached(&ev->node, tree, timer_ev_less);
    return ret ? ev : NULL;
}

static inline struct timer_event *
timer_ev_del(struct timer_event *ev, struct rb_root_cached *tree)
{
    struct rb_node *ret = rb_erase_cached(&ev->node, tree);
    RB_CLEAR_NODE(&ev->node);
    return ret ? ev : NULL;
}

void timer_ev_default_callback(struct timer_event *ev)
{
    set_task_running(ev->p);
}

struct timer_event * create_timer_event(struct task *p, ktime_t expires,
    void (*callback)(struct timer_event *), void *context)
{
    struct timer_event *ev = kmalloc(sizeof(*ev));
    if (!ev)
        return NULL;
    ev->p = p;
    ev->expires = expires;
    ev->callback = callback ? callback : timer_ev_default_callback;
    ev->context = context;
    INIT_LIST_HEAD(&ev->task_list);
    RB_CLEAR_NODE(&ev->node);
    return ev;
}

void destroy_timer_event(struct timer_event *ev)
{
    kfree(ev);
}

static void __timer_ev_enqueue(struct timer_event *ev, struct task *p)
{
    ev->base = get_cpu_var(timer_bases);
    timer_ev_add(ev, &ev->base->tree);
    list_add_tail(&ev->task_list, &p->timer_ev_list);
}

void timer_ev_enqueue(struct timer_event *ev, struct task *p)
{
    unsigned long flags;
    acquire_lock_irqsave(&timer_lock, &flags);
    __timer_ev_enqueue(ev, p);
    release_lock_irqrestore(&timer_lock, flags);
}

static bool __timer_ev_dequeue(struct timer_event *ev)
{
    if (!timer_ev_queued(ev))
        return false;
    timer_ev_del(ev, &ev->base->tree);
    list_del_init(&ev->task_list);
    return true;
}

/*
 * Returns true if the event was removed before it fired. Otherwise it has
 * already fired, and this waits for its callback to finish.
 */
bool timer_ev_dequeue(struct timer_event *ev)
{
    unsigned long flags;
    bool removed;

    acquire_lock_irqsave(&timer_lock, &flags);
    removed = __timer_ev_dequeue(ev);
    while (!removed && ev->base && ev->base->running == ev) {
        release_lock_irqrestore(&timer_lock, flags);
        __pause();
        acquire_lock_irqsave(&timer_lock, &flags);
    }
    release_lock_irqrestore(&timer_lock, flags);
    return removed;
}

void timer_ev_tick(void)
{
    ktime_t now_ns = ktime_get();
    struct timer_base *base = get_cpu_var(timer_bases);
    struct rb_node *node;

    acquire_lock(&timer_lock); // interrupt context
    while ((node = base->tree.rb_leftmost) != NULL) {
        struct timer_event *ev = rb_entry(node, struct timer_event, node);
        if (ev->expires > now_ns)
            break;

        __timer_ev_dequeue(ev);
        base->running = ev;
        base->running_task = ev->p;
        release_lock(&timer_lock);
        ev->callback(ev);
        acquire_lock(&timer_lock);
        base->running = NULL;
        base->running_task = NULL;
    }
    release_lock(&timer_lock);
}

__attribute__((optimize("O0")))
void busy_wait_usec(u32 micros)
{
    u64 start = ktime_get();
    u64 end = start + (u64)micros * 1000;
    while (ktime_get() < end)
        __pause();
}

static void sleep_until(ktime_t end)
{
    struct timer_event ev = TIMER_EV_INIT(ev, current, end, NULL, NULL);
    set_task_sleeping(current);
    timer_ev_enqueue(&ev, current);
    schedule();
    timer_ev_dequeue(&ev);
}

__attribute__((optimize("O0")))
void usleep(u32 micros)
{
    ktime_t end = ktime_add_ns(ktime_get(), (u64)micros * 1000);
    if (micros >= 1000) {
        sleep_until(end);
    } else {
        while (ktime_get() < end)
            __pause();
    }
}

#define TIMER_ABSTIME 1

static long nanosleep_restart(struct restart_block *rb);

// Sleep to monotonic deadline
static long nanosleep_until(ktime_t deadline, struct timespec __user *rem, bool relative)
{
    struct timespec krem;

    sleep_until(deadline);
    if (!task_interrupted_ack())
        return 0;

    if (!relative)
        return -ERESTARTNOHAND;
    if (rem) {
        ktime_t left = deadline - ktime_get();
        if (left < 0)
            left = 0;
        krem.tv_sec = left / NS_PER_SEC;
        krem.tv_nsec = left % NS_PER_SEC;
        if (copy_to_user(rem, &krem, sizeof(struct timespec)))
            return -EFAULT;
    }
    current->restart_block = (struct restart_block){
        .fn = nanosleep_restart,
        .deadline = deadline,
        .rem = rem,
    };
    return -ERESTART_RESTARTBLOCK;
}

static long nanosleep_restart(struct restart_block *rb)
{
    if (rb->deadline <= ktime_get())
        return 0;
    return nanosleep_until(rb->deadline, rb->rem, true);
}

static long do_nanosleep(int clk, int flags, const struct timespec *req,
                         struct timespec *rem)
{
    struct timespec kreq;
    if (copy_from_user(&kreq, req, sizeof(struct timespec)))
        return -EFAULT;
    if (kreq.tv_sec < 0 || kreq.tv_nsec < 0 || kreq.tv_nsec >= NS_PER_SEC)
        return -EINVAL;

    ktime_t now = ktime_get();
    ktime_t req_ns = timespec_to_ktime(kreq);

    switch (clk) {
    case CLOCK_REALTIME:
    case CLOCK_REALTIME_COARSE:
        if (flags & TIMER_ABSTIME)
            req_ns -= boot_unix_time * NS_PER_SEC;
        break;
    case CLOCK_MONOTONIC:
    case CLOCK_MONOTONIC_RAW:
    case CLOCK_MONOTONIC_COARSE:
    case CLOCK_BOOTTIME:
        break;
    default:
        return -EINVAL;
    }

    ktime_t deadline = (flags & TIMER_ABSTIME) ? req_ns : ktime_add_sat(now, req_ns);
    if (deadline <= now)
        return 0;

    return nanosleep_until(deadline, rem, !(flags & TIMER_ABSTIME));
}

SYSCALL_DECL2(nanosleep, const struct timespec*, duration, struct timespec*, rem)
{
    return do_nanosleep(CLOCK_MONOTONIC, 0, duration, rem);
}

SYSCALL_DECL4(clock_nanosleep, int, clk, int, flags,
              const struct timespec*, req, struct timespec*, rem)
{
    return do_nanosleep(clk, flags, req, rem);
}

// Cancel a dead task's pending events
void timer_ev_task_exit(struct task *p)
{
    struct timer_event *ev, *tmp;
    unsigned long flags;

    acquire_lock_irqsave(&timer_lock, &flags);
    list_for_each_entry_safe(ev, tmp, &p->timer_ev_list, task_list) {
        // ITIMER_REAL belongs to the process
        if (ev != &p->itimer_real.ev)
            __timer_ev_dequeue(ev);
    }

    // A callback already running elsewhere (e.g. an alarm raising a signal)
    // still uses p; the task must not be reaped until it finishes
    for (int cpu = 0; cpu < boot_info.ncpus; cpu++) {
        struct timer_base *base = per_cpu_ptr(&timer_bases, cpu);
        while (base->running_task == p) {
            release_lock_irqrestore(&timer_lock, flags);
            __pause();
            acquire_lock_irqsave(&timer_lock, &flags);
        }
    }
    release_lock_irqrestore(&timer_lock, flags);
}

#define ITIMER_REAL 0

struct itimerval {
    struct timeval it_interval;
    struct timeval it_value;
};

static void itimer_real_fn(struct timer_event *ev)
{
    struct task *p = ev->p;
    ktime_t interval = READ_ONCE(p->itimer_real.interval);

    if (!do_raise(p, SIGALRM))
        return;
    if (interval) {
        ktime_t now = ktime_get();
        ev->expires = ktime_add_sat(ev->expires, interval);
        if (ev->expires <= now)
            ev->expires = ktime_add_sat(now, interval);
        timer_ev_enqueue(ev, p);
    }
}

void init_itimer_real(struct task *p)
{
    struct timer_event *ev = &p->itimer_real.ev;
    *ev = (struct timer_event){ .p = p, .callback = itimer_real_fn };
    RB_CLEAR_NODE(&ev->node);
    INIT_LIST_HEAD(&ev->task_list);
    p->itimer_real.interval = 0;
}

/*
 * Replace leader p's ITIMER_REAL. A zero value disarms it. Optionally
 * returns the time that was left (0 if unarmed) and the old interval.
 */
static void itimer_real_set(struct task *p, ktime_t value, ktime_t interval,
                            ktime_t *old_left, ktime_t *old_interval)
{
    struct timer_event *ev = &p->itimer_real.ev;
    unsigned long flags;
    ktime_t left = 0;

    acquire_lock_irqsave(&timer_lock, &flags);
    while (ev->base && ev->base->running == ev) {
        release_lock_irqrestore(&timer_lock, flags);
        __pause();
        acquire_lock_irqsave(&timer_lock, &flags);
    }
    if (old_interval)
        *old_interval = p->itimer_real.interval;
    if (__timer_ev_dequeue(ev)) {
        left = ev->expires - ktime_get();
        if (left <= 0)
            left = 1;
    }
    WRITE_ONCE(p->itimer_real.interval, value ? interval : 0);
    if (value) {
        ev->expires = ktime_add_sat(ktime_get(), value);
        __timer_ev_enqueue(ev, p);
    }
    release_lock_irqrestore(&timer_lock, flags);

    if (old_left)
        *old_left = left;
}

// Disarm process timer
void itimer_real_exit(struct task *p)
{
    itimer_real_set(p, 0, 0, NULL, NULL);
}

static bool timeval_valid(const struct timeval *tv)
{
    return tv->tv_sec >= 0 && tv->tv_usec >= 0 && tv->tv_usec < 1000000;
}

SYSCALL_DECL3(setitimer, int, which, const struct itimerval*, new,
              struct itimerval*, old)
{
    struct itimerval knew = {0}, kold;
    ktime_t left, old_interval;

    if (which != ITIMER_REAL)
        return -EINVAL;
    if (new) {
        if (copy_from_user(&knew, new, sizeof(knew)))
            return -EFAULT;
        if (!timeval_valid(&knew.it_value) || !timeval_valid(&knew.it_interval))
            return -EINVAL;
    }

    itimer_real_set(current->tg_leader, timeval_to_ns(&knew.it_value),
                    timeval_to_ns(&knew.it_interval), &left, &old_interval);

    if (old) {
        kold.it_value = left ? ns_to_timeval(left) : (struct timeval){0};
        kold.it_interval = ns_to_timeval(old_interval);
        if (copy_to_user(old, &kold, sizeof(kold)))
            return -EFAULT;
    }
    return 0;
}

SYSCALL_DECL2(getitimer, int, which, struct itimerval*, cur)
{
    struct itimerval kcur = {0};
    struct task *p = current->tg_leader;
    unsigned long flags;

    if (which != ITIMER_REAL)
        return -EINVAL;

    acquire_lock_irqsave(&timer_lock, &flags);
    if (timer_ev_queued(&p->itimer_real.ev)) {
        ktime_t left = p->itimer_real.ev.expires - ktime_get();
        kcur.it_value = ns_to_timeval(left > 0 ? left : 1);
    }
    kcur.it_interval = ns_to_timeval(p->itimer_real.interval);
    release_lock_irqrestore(&timer_lock, flags);

    return copy_to_user(cur, &kcur, sizeof(kcur)) ? -EFAULT : 0;
}

SYSCALL_DECL1(alarm, unsigned int, seconds)
{
    struct task *p = current->tg_leader;
    ktime_t left;

    klog(LOG_DEBUG, "Process %d set alarm for %u seconds\n", p->pid, seconds);

    itimer_real_set(p, (ktime_t)seconds * NS_PER_SEC, 0, &left, NULL);

    return left / NS_PER_SEC + (left % NS_PER_SEC != 0);
}

static int is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int days_in_month(int month, int year) {
    if (month == 2) {
        return is_leap_year(year) ? 29 : 28;
    }
    static const int days_in_months[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
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
