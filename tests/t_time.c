// Time: clocks, sleeping.
#include "ktest.h"
#include <sys/time.h>
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
