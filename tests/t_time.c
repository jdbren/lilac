// Time: clocks, sleeping.
#include "ktest.h"
#include <pthread.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/times.h>
#include <time.h>

static long long ts_ns(struct timespec ts)
{
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

TEST(time_plausible)
{
    time_t t = time(NULL);
    EXPECT_GT(t, 1577836800); /* 2020-01-01 */
    EXPECT_LT(t, 4102444800); /* 2100-01-01 */
    time_t t2;
    EXPECT_EQ(time(&t2), t2);
}

TEST(gettimeofday_plausible_and_advances)
{
    struct timeval a, b;
    ASSERT_OK(gettimeofday(&a, NULL));
    EXPECT_GT(a.tv_sec, 1577836800);
    EXPECT_TRUE(a.tv_usec >= 0 && a.tv_usec < 1000000);
    usleep(20000);
    ASSERT_OK(gettimeofday(&b, NULL));
    long long d = (b.tv_sec - a.tv_sec) * 1000000LL + (b.tv_usec - a.tv_usec);
    EXPECT_GE(d, 15000);
    EXPECT_LT(d, 1000000);
}

TEST(gettimeofday_matches_time)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t t = time(NULL);
    EXPECT_LE(t - tv.tv_sec, 1);
    EXPECT_GE(t - tv.tv_sec, 0);
}

TEST(clock_realtime)
{
    struct timespec ts;
    ASSERT_OK(clock_gettime(CLOCK_REALTIME, &ts));
    EXPECT_GT(ts.tv_sec, 1577836800);
    EXPECT_TRUE(ts.tv_nsec >= 0 && ts.tv_nsec < 1000000000);
}

TEST(clock_monotonic)
{
    struct timespec a, b;
    ASSERT_OK(clock_gettime(CLOCK_MONOTONIC, &a));
    for (int i = 0; i < 10000; i++) {
        ASSERT_OK(clock_gettime(CLOCK_MONOTONIC, &b));
        if (ts_ns(b) < ts_ns(a)) {
            FAIL("went backwards: %lld -> %lld", ts_ns(a), ts_ns(b));
            break;
        }
        a = b;
    }
}

TEST(clock_monotonic_resolution)
{
    struct timespec a, b;
    ASSERT_OK(clock_gettime(CLOCK_MONOTONIC, &a));
    usleep(1000);
    ASSERT_OK(clock_gettime(CLOCK_MONOTONIC, &b));
    long long d = ts_ns(b) - ts_ns(a);
    EXPECT_GE(d, 900000);
    EXPECT_LT(d, 100000000);
    struct timespec res;
    EXPECT_OK(clock_getres(CLOCK_MONOTONIC, &res));
}

TEST(clock_process_cputime)
{
    struct timespec a, b;
    ASSERT_OK(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &a));
    for (volatile int i = 0; i < 5000000; i++)
        ;
    ASSERT_OK(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &b));
    EXPECT_GT(ts_ns(b), ts_ns(a));
}

TEST(nanosleep_duration)
{
    long long durations[] = { 1000000, 10000000, 100000000, 250000000 };
    for (unsigned i = 0; i < 4; i++) {
        struct timespec req = { 0, durations[i] };
        long long t0 = ktest_now_ns();
        EXPECT_OK(nanosleep(&req, NULL));
        long long el = ktest_now_ns() - t0;
        if (el < durations[i])
            FAIL("nanosleep(%lldms) returned early after %lldus",
                 durations[i] / 1000000, el / 1000);
        if (el > durations[i] + 50000000)
            FAIL("nanosleep(%lldms) overslept: %lldms",
                 durations[i] / 1000000, el / 1000000);
    }
}

TEST(nanosleep_invalid)
{
    struct timespec bad = { 0, 1000000000 };
    EXPECT_ERR(nanosleep(&bad, NULL), EINVAL);
    struct timespec neg = { -1, 0 };
    EXPECT_ERR(nanosleep(&neg, NULL), EINVAL);
}

TEST(sleep_one_second)
{
    long long t0 = ktest_now_ns();
    EXPECT_EQ(sleep(1), 0);
    long long ms = (ktest_now_ns() - t0) / 1000000;
    EXPECT_GE(ms, 1000);
    EXPECT_LT(ms, 1200);
}

TEST(usleep_zero)
{
    EXPECT_OK(usleep(0));
}

TEST(clock_nanosleep_abs)
{
    struct timespec now;
    ASSERT_OK(clock_gettime(CLOCK_MONOTONIC, &now));
    now.tv_nsec += 50000000;
    if (now.tv_nsec >= 1000000000) {
        now.tv_sec++;
        now.tv_nsec -= 1000000000;
    }
    EXPECT_EQ(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &now, NULL), 0);
    struct timespec after;
    clock_gettime(CLOCK_MONOTONIC, &after);
    EXPECT_GE(ts_ns(after), ts_ns(now));
}

TEST(sleeping_does_not_burn_cpu)
{
    struct timespec a, b;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &a) != 0)
        SKIP("no CLOCK_PROCESS_CPUTIME_ID");
    usleep(300000);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &b);
    EXPECT_LT(ts_ns(b) - ts_ns(a), 100000000);
}

TEST(concurrent_sleepers_wake)
{
    enum { N = 8 };
    pid_t pids[N];
    long long t0 = ktest_now_ns();
    for (int i = 0; i < N; i++) {
        pids[i] = ASSERT_OK(fork());
        if (pids[i] == 0) {
            usleep(200000);
            _exit(0);
        }
    }
    for (int i = 0; i < N; i++) {
        int st = ktest_wait_timeout(pids[i], 5);
        EXPECT_TRUE(st != -1 && WIFEXITED(st));
    }
    long long ms = (ktest_now_ns() - t0) / 1000000;
    EXPECT_LT(ms, 1000);
}

/* ================= times / getrusage ================= */

static long long tv_ns(struct timeval tv)
{
    return tv.tv_sec * 1000000000LL + tv.tv_usec * 1000LL;
}

static long long thread_cpu_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return ts_ns(ts);
}

// Burn ms of CPU time, nearly all of it in user mode
static void spin_user_ms(long ms)
{
    long long end = thread_cpu_ns() + ms * 1000000LL;
    while (thread_cpu_ns() < end)
        for (volatile int i = 0; i < 200000; i++)
            ;
}

// Burn ms of CPU time, mostly in the kernel: /dev/zero reads clear the
// buffer in the kernel
static void spin_kernel_ms(long ms)
{
    static char buf[1 << 20];
    int fd = open("/dev/zero", O_RDONLY);
    if (fd < 0) {
        FAIL("open /dev/zero: errno=%d", errno);
        ktest_abort();
    }
    long long end = thread_cpu_ns() + ms * 1000000LL;
    while (thread_cpu_ns() < end)
        if (read(fd, buf, sizeof(buf)) != sizeof(buf)) {
            FAIL("read /dev/zero: errno=%d", errno);
            ktest_abort();
        }
    close(fd);
}

static void usage(int who, long long *user, long long *sys)
{
    struct rusage ru;
    if (getrusage(who, &ru) != 0) {
        FAIL("getrusage(%d): errno=%d", who, errno);
        ktest_abort();
    }
    *user = tv_ns(ru.ru_utime);
    *sys = tv_ns(ru.ru_stime);
}

TEST(getrusage_user_time)
{
    long long u0, s0, u1, s1;
    usage(RUSAGE_SELF, &u0, &s0);
    spin_user_ms(300);
    usage(RUSAGE_SELF, &u1, &s1);
    EXPECT_GE(u1 - u0, 200000000LL);
    EXPECT_LT(s1 - s0, u1 - u0);
}

TEST(getrusage_system_time)
{
    long long u0, s0, u1, s1;
    usage(RUSAGE_SELF, &u0, &s0);
    spin_kernel_ms(300);
    usage(RUSAGE_SELF, &u1, &s1);
    EXPECT_GE(s1 - s0, 100000000LL);
    EXPECT_GT(s1 - s0, u1 - u0);
}

TEST(getrusage_fields_valid)
{
    spin_user_ms(50);
    struct rusage ru;
    ASSERT_OK(getrusage(RUSAGE_SELF, &ru));
    EXPECT_TRUE(ru.ru_utime.tv_usec >= 0 && ru.ru_utime.tv_usec < 1000000);
    EXPECT_TRUE(ru.ru_stime.tv_usec >= 0 && ru.ru_stime.tv_usec < 1000000);
    EXPECT_ERR(getrusage(5, &ru), EINVAL);
    EXPECT_ERR(getrusage(RUSAGE_SELF, (struct rusage *)1), EFAULT);
}

// A forked child starts at zero, not at its parent's totals
TEST(getrusage_fork_starts_at_zero)
{
    spin_user_ms(300);
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        long long u, s;
        usage(RUSAGE_SELF, &u, &s);
        _exit(u + s < 100000000LL ? 0 : 1);
    }
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

TEST(getrusage_children_after_wait)
{
    long long u0, s0, u1, s1, su0, ss0, su1, ss1;
    usage(RUSAGE_CHILDREN, &u0, &s0);
    usage(RUSAGE_SELF, &su0, &ss0);
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        spin_user_ms(300);
        _exit(0);
    }
    // let it finish, but don't reap it yet
    usleep(600000);
    usage(RUSAGE_CHILDREN, &u1, &s1);
    EXPECT_EQ(u1, u0);
    int st;
    ASSERT_EQ(waitpid(pid, &st, 0), pid);
    usage(RUSAGE_CHILDREN, &u1, &s1);
    EXPECT_GE(u1 - u0, 200000000LL);
    // the child's time isn't the parent's own
    usage(RUSAGE_SELF, &su1, &ss1);
    EXPECT_LT(su1 - su0, 100000000LL);
}

// A child's total includes the children it waited for
TEST(getrusage_children_include_grandchildren)
{
    long long u0, s0, u1, s1;
    usage(RUSAGE_CHILDREN, &u0, &s0);
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        pid_t g = fork();
        if (g == 0) {
            spin_user_ms(300);
            _exit(0);
        }
        int gst;
        _exit(waitpid(g, &gst, 0) == g ? 0 : 1);
    }
    int st = ktest_wait_timeout(pid, 10);
    EXPECT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    usage(RUSAGE_CHILDREN, &u1, &s1);
    EXPECT_GE(u1 - u0, 200000000LL);
}

static void *spin_thread(void *arg)
{
    spin_user_ms((long)arg);
    return NULL;
}

// RUSAGE_SELF counts every thread, exited ones included; RUSAGE_THREAD
// only the caller
TEST(getrusage_threads)
{
    long long u0, s0, u1, s1, tu0, ts0, tu1, ts1;
    usage(RUSAGE_SELF, &u0, &s0);
    usage(RUSAGE_THREAD, &tu0, &ts0);
    pthread_t t;
    ASSERT_EQ(pthread_create(&t, NULL, spin_thread, (void *)300), 0);
    ASSERT_EQ(pthread_join(t, NULL), 0);
    usage(RUSAGE_SELF, &u1, &s1);
    usage(RUSAGE_THREAD, &tu1, &ts1);
    EXPECT_GE(u1 - u0, 200000000LL);
    EXPECT_LT(tu1 - tu0, 100000000LL);
}

TEST(clock_process_cputime_counts_threads)
{
    struct timespec a, b;
    ASSERT_OK(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &a));
    pthread_t t;
    ASSERT_EQ(pthread_create(&t, NULL, spin_thread, (void *)300), 0);
    ASSERT_EQ(pthread_join(t, NULL), 0);
    ASSERT_OK(clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &b));
    EXPECT_GE(ts_ns(b) - ts_ns(a), 200000000LL);
}

TEST(times_counts_cpu_and_children)
{
    long hz = sysconf(_SC_CLK_TCK);
    ASSERT_GT(hz, 0);
    struct tms t0, t1;
    clock_t r0 = times(&t0);
    spin_user_ms(300);
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        spin_user_ms(300);
        _exit(0);
    }
    int st;
    ASSERT_EQ(waitpid(pid, &st, 0), pid);
    clock_t r1 = times(&t1);
    EXPECT_GE(t1.tms_utime - t0.tms_utime, hz / 5);
    EXPECT_GE(t1.tms_cutime - t0.tms_cutime, hz / 5);
    // the return value is elapsed real time, at least the CPU time used
    EXPECT_GE(r1 - r0, (t1.tms_utime - t0.tms_utime) + (t1.tms_cutime - t0.tms_cutime) - 2);
}

TEST(times_elapsed_and_null)
{
    long hz = sysconf(_SC_CLK_TCK);
    clock_t r0 = times(NULL);
    ASSERT_NE(r0, (clock_t)-1);
    usleep(500000);
    clock_t r1 = times(NULL);
    long ms = (r1 - r0) * 1000 / hz;
    EXPECT_GE(ms, 450);
    EXPECT_LT(ms, 1500);
    // sleeping doesn't use CPU
    struct tms t0, t1;
    times(&t0);
    usleep(300000);
    times(&t1);
    EXPECT_LT((t1.tms_utime + t1.tms_stime) - (t0.tms_utime + t0.tms_stime), hz / 10);
}
