// Threads: pthread create/join, mutexes, condvars, TLS, futex, SMP.
#include "ktest.h"
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/syscall.h>

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_WAKE_PRIVATE (FUTEX_WAKE | 128)

static void *ret_arg_plus_one(void *arg)
{
    return (void *)((uintptr_t)arg + 1);
}

TEST(create_join_retval)
{
    pthread_t t;
    ASSERT_EQ(pthread_create(&t, NULL, ret_arg_plus_one, (void *)41), 0);
    void *ret = NULL;
    EXPECT_EQ(pthread_join(t, &ret), 0);
    EXPECT_EQ((uintptr_t)ret, 42);
}

TEST(many_threads_sequential)
{
    for (int i = 0; i < 100; i++) {
        pthread_t t;
        int r = pthread_create(&t, NULL, ret_arg_plus_one, (void *)(uintptr_t)i);
        if (r != 0) {
            FAIL("create #%d failed: %d", i, r);
            ktest_abort();
        }
        void *ret;
        pthread_join(t, &ret);
        if ((uintptr_t)ret != (uintptr_t)i + 1) {
            FAIL("thread %d returned %p", i, ret);
            break;
        }
    }
}

static pid_t thread_pid, thread_tid;
static void *ids(void *arg)
{
    thread_pid = getpid();
    thread_tid = gettid();
    return NULL;
}

TEST(tid_differs_pid_shared)
{
    pthread_t t;
    ASSERT_EQ(pthread_create(&t, NULL, ids, NULL), 0);
    pthread_join(t, NULL);
    EXPECT_EQ(thread_pid, getpid());
    EXPECT_NE(thread_tid, gettid());
    EXPECT_EQ(gettid(), getpid()); /* main thread tid == pid */
}

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static long counter;
enum { NTHREADS = 8, ITERS = 20000 };

static void *inc_locked(void *arg)
{
    for (int i = 0; i < ITERS; i++) {
        pthread_mutex_lock(&mtx);
        counter++;
        pthread_mutex_unlock(&mtx);
    }
    return NULL;
}

TEST_TIMEOUT(mutex_counter, 30)
{
    counter = 0;
    pthread_t t[NTHREADS];
    for (int i = 0; i < NTHREADS; i++)
        ASSERT_EQ(pthread_create(&t[i], NULL, inc_locked, NULL), 0);
    for (int i = 0; i < NTHREADS; i++)
        pthread_join(t[i], NULL);
    EXPECT_EQ(counter, (long)NTHREADS * ITERS);
}

static atomic_long acounter;
static void *inc_atomic(void *arg)
{
    for (int i = 0; i < 100000; i++)
        atomic_fetch_add(&acounter, 1);
    return NULL;
}

TEST_TIMEOUT(atomic_counter_smp, 30)
{
    atomic_store(&acounter, 0);
    pthread_t t[4];
    for (int i = 0; i < 4; i++)
        ASSERT_EQ(pthread_create(&t[i], NULL, inc_atomic, NULL), 0);
    for (int i = 0; i < 4; i++)
        pthread_join(t[i], NULL);
    EXPECT_EQ(atomic_load(&acounter), 400000);
}

static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int turn, rounds_done;

static void *pong(void *arg)
{
    for (int i = 0; i < 1000; i++) {
        pthread_mutex_lock(&mtx);
        while (turn != 1)
            pthread_cond_wait(&cv, &mtx);
        turn = 0;
        rounds_done++;
        pthread_cond_broadcast(&cv);
        pthread_mutex_unlock(&mtx);
    }
    return NULL;
}

TEST_TIMEOUT(condvar_ping_pong, 30)
{
    turn = 0;
    rounds_done = 0;
    pthread_t t;
    ASSERT_EQ(pthread_create(&t, NULL, pong, NULL), 0);
    for (int i = 0; i < 1000; i++) {
        pthread_mutex_lock(&mtx);
        while (turn != 0)
            pthread_cond_wait(&cv, &mtx);
        turn = 1;
        pthread_cond_broadcast(&cv);
        pthread_mutex_unlock(&mtx);
    }
    pthread_join(t, NULL);
    EXPECT_EQ(rounds_done, 1000);
}

static _Thread_local int tls_var = 5;
static void *tls_thread(void *arg)
{
    if (tls_var != 5)
        return (void *)1;
    tls_var = (int)(uintptr_t)arg;
    usleep(10000);
    return tls_var == (int)(uintptr_t)arg ? NULL : (void *)2;
}

TEST(thread_local_storage)
{
    tls_var = 99;
    pthread_t t[4];
    for (int i = 0; i < 4; i++)
        ASSERT_EQ(pthread_create(&t[i], NULL, tls_thread, (void *)(uintptr_t)(i + 10)), 0);
    for (int i = 0; i < 4; i++) {
        void *r;
        pthread_join(t[i], &r);
        EXPECT_EQ((uintptr_t)r, 0);
    }
    EXPECT_EQ(tls_var, 99);
}

static int shared_val;
static void *write_shared(void *arg)
{
    shared_val = 1234;
    return NULL;
}

TEST(threads_share_memory)
{
    shared_val = 0;
    pthread_t t;
    ASSERT_EQ(pthread_create(&t, NULL, write_shared, NULL), 0);
    pthread_join(t, NULL);
    EXPECT_EQ(shared_val, 1234);
}

static atomic_int futex_word;
static void *futex_waker(void *arg)
{
    usleep(100000);
    atomic_store(&futex_word, 1);
    syscall(SYS_futex, &futex_word, FUTEX_WAKE, 1, NULL, NULL, 0);
    return NULL;
}

TEST(futex_wait_wake)
{
    atomic_store(&futex_word, 0);
    pthread_t t;
    ASSERT_EQ(pthread_create(&t, NULL, futex_waker, NULL), 0);
    while (atomic_load(&futex_word) == 0) {
        long r = syscall(SYS_futex, &futex_word, FUTEX_WAIT, 0, NULL, NULL, 0);
        if (r != 0 && errno != EAGAIN && errno != EINTR) {
            FAIL("futex wait errno=%d", errno);
            break;
        }
    }
    EXPECT_EQ(atomic_load(&futex_word), 1);
    pthread_join(t, NULL);
}

TEST(futex_wait_value_mismatch)
{
    int word = 5;
    EXPECT_ERR(syscall(SYS_futex, &word, FUTEX_WAIT, 4, NULL, NULL, 0), EAGAIN);
}

TEST(futex_wait_timeout)
{
    int word = 0;
    struct timespec ts = { 0, 100000000 };
    long long t0 = ktest_now_ns();
    EXPECT_ERR(syscall(SYS_futex, &word, FUTEX_WAIT, 0, &ts, NULL, 0), ETIMEDOUT);
    long long ms = (ktest_now_ns() - t0) / 1000000;
    EXPECT_GE(ms, 90);
    EXPECT_LE(ms, 1000);
}

TEST(futex_private_ops)
{
    int word = 0;
    EXPECT_EQ(syscall(SYS_futex, &word, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0), 0);
}

static void *detached_fn(void *arg)
{
    atomic_store((atomic_int *)arg, 1);
    return NULL;
}

TEST(detached_thread)
{
    static atomic_int flag;
    atomic_store(&flag, 0);
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    pthread_t t;
    ASSERT_EQ(pthread_create(&t, &a, detached_fn, &flag), 0);
    for (int i = 0; i < 200 && !atomic_load(&flag); i++)
        usleep(5000);
    EXPECT_EQ(atomic_load(&flag), 1);
}

static void *exit_group_fn(void *arg)
{
    usleep(50000);
    exit(33);
}

TEST(exit_from_thread_ends_process)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        pthread_t t;
        pthread_create(&t, NULL, exit_group_fn, NULL);
        for (;;)
            pause();
    }
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(st != -1 && WIFEXITED(st));
    EXPECT_EQ(WEXITSTATUS(st), 33);
}

static void *pthread_exit_fn(void *arg)
{
    pthread_exit((void *)77);
    return NULL;
}

TEST(pthread_exit_value)
{
    pthread_t t;
    ASSERT_EQ(pthread_create(&t, NULL, pthread_exit_fn, NULL), 0);
    void *r;
    pthread_join(t, &r);
    EXPECT_EQ((uintptr_t)r, 77);
}

static void *spin_forever(void *arg)
{
    for (;;)
        ;
    return NULL;
}

TEST(main_exit_kills_threads)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        pthread_t t;
        pthread_create(&t, NULL, spin_forever, NULL);
        usleep(20000);
        _exit(4);
    }
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 4);
}

TEST(kill_process_with_threads)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        pthread_t t[3];
        for (int i = 0; i < 3; i++)
            pthread_create(&t[i], NULL, spin_forever, NULL);
        for (;;)
            pause();
    }
    usleep(50000);
    kill(pid, SIGKILL);
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);
}

static void *pause_forever(void *arg)
{
    for (;;)
        pause();
    return NULL;
}

/* Child spawns nthreads paused threads that share a pipe's write end, then
 * exit_group()s. EOF on the read end means every thread is gone. */
static void exit_group_kills_n(int nthreads)
{
    int fds[2];
    ASSERT_OK(pipe(fds));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        close(fds[0]);
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 64 * 1024);
        for (int i = 0; i < nthreads; i++) {
            pthread_t t;
            if (pthread_create(&t, &attr, pause_forever, NULL) != 0)
                _exit(2);
        }
        usleep(50000);
        _exit(0);
    }
    close(fds[1]);

    int st = ktest_wait_timeout(pid, 10);
    EXPECT_TRUE(st != -1 && WIFEXITED(st));
    if (st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 2)
        FAIL("pthread_create failed before %d threads", nthreads);

    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    char c;
    long long deadline = ktest_now_ns() + 5LL * 1000000000;
    for (;;) {
        ssize_t n = read(fds[0], &c, 1);
        if (n == 0)
            break;
        if (n < 0 && errno != EAGAIN) {
            FAIL("read: errno=%d (%s)", errno, strerror(errno));
            break;
        }
        if (ktest_now_ns() > deadline) {
            FAIL("threads outlived exit_group with %d threads", nthreads);
            break;
        }
        usleep(10000);
    }
    close(fds[0]);
}

TEST_TIMEOUT(exit_group_kills_few_threads, 30)
{
    exit_group_kills_n(8);
}

/* more threads than zap_other_threads used to collect in one pass */
TEST_TIMEOUT(exit_group_kills_many_threads, 30)
{
    exit_group_kills_n(100);
}

static void *segv_thread(void *arg)
{
    *(volatile int *)0 = 1;
    return NULL;
}

TEST(fault_in_thread_kills_process)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        pthread_t t;
        pthread_create(&t, NULL, segv_thread, NULL);
        for (;;)
            pause();
    }
    int st = ktest_wait_timeout(pid, 5);
    if (st == -1)
        FAIL("process did not die (SIGSEGV only killed the faulting thread?)");
    else if (!WIFSIGNALED(st) || WTERMSIG(st) != SIGSEGV)
        FAIL("expected death by SIGSEGV, wait status 0x%x", st);
}

static volatile sig_atomic_t thread_sig_hits;
static void thread_sig_handler(int s) { thread_sig_hits++; }
static void *wait_for_sig(void *arg)
{
    for (int i = 0; i < 200 && !thread_sig_hits; i++)
        usleep(5000);
    return NULL;
}

TEST(pthread_kill_delivers)
{
    thread_sig_hits = 0;
    signal(SIGUSR1, thread_sig_handler);
    pthread_t t;
    ASSERT_EQ(pthread_create(&t, NULL, wait_for_sig, NULL), 0);
    usleep(10000);
    EXPECT_EQ(pthread_kill(t, SIGUSR1), 0);
    pthread_join(t, NULL);
    EXPECT_EQ(thread_sig_hits, 1);
}

static void *fork_in_thread(void *arg)
{
    pid_t pid = fork();
    if (pid == 0)
        _exit(8);
    int st;
    waitpid(pid, &st, 0);
    return (void *)(uintptr_t)(WIFEXITED(st) ? WEXITSTATUS(st) : 0);
}

TEST(fork_from_thread)
{
    pthread_t t;
    ASSERT_EQ(pthread_create(&t, NULL, fork_in_thread, NULL), 0);
    void *r;
    pthread_join(t, &r);
    EXPECT_EQ((uintptr_t)r, 8);
}

static void *malloc_stress(void *arg)
{
    for (int i = 0; i < 2000; i++) {
        void *p = malloc(16 + (i * 13) % 2000);
        if (!p)
            return (void *)1;
        memset(p, i, 16);
        free(p);
    }
    return NULL;
}

TEST_TIMEOUT(concurrent_malloc, 30)
{
    pthread_t t[4];
    for (int i = 0; i < 4; i++)
        ASSERT_EQ(pthread_create(&t[i], NULL, malloc_stress, NULL), 0);
    for (int i = 0; i < 4; i++) {
        void *r;
        pthread_join(t[i], &r);
        EXPECT_EQ((uintptr_t)r, 0);
    }
}

static void *mmap_stress(void *arg)
{
    for (int i = 0; i < 200; i++) {
        char *p = mmap(NULL, 16 * 4096, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED)
            return (void *)1;
        for (int j = 0; j < 16; j++)
            p[j * 4096] = (char)j;
        munmap(p, 16 * 4096);
    }
    return NULL;
}

TEST_TIMEOUT(concurrent_mmap_munmap, 30)
{
    /* exercises TLB shootdown between CPUs running the same mm */
    pthread_t t[4];
    for (int i = 0; i < 4; i++)
        ASSERT_EQ(pthread_create(&t[i], NULL, mmap_stress, NULL), 0);
    for (int i = 0; i < 4; i++) {
        void *r;
        pthread_join(t[i], &r);
        EXPECT_EQ((uintptr_t)r, 0);
    }
}

static void *spin_report(void *arg)
{
    long long t0 = ktest_now_ns();
    while (ktest_now_ns() - t0 < 300000000LL)
        ;
    return NULL;
}

TEST(threads_run_in_parallel)
{
    /* 4 threads spinning 300ms each should take well under 1.2s on 4 CPUs */
    /* qemu runs with -smp 4; don't trust sysconf (needs sched_getaffinity) */
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    pthread_t t[4];
    long long t0 = ktest_now_ns();
    for (int i = 0; i < 4; i++)
        ASSERT_EQ(pthread_create(&t[i], NULL, spin_report, NULL), 0);
    for (int i = 0; i < 4; i++)
        pthread_join(t[i], NULL);
    long long ms = (ktest_now_ns() - t0) / 1000000;
    ktest_log("4x300ms spin took %lldms on %ld cpus", ms, cpus);
    EXPECT_LT(ms, 1000);
}

TEST(sched_yield_ok)
{
    for (int i = 0; i < 100; i++)
        EXPECT_OK(sched_yield());
}

/* ---- pthread_cancel: musl delivers SIGCANCEL, and its handler reads and
 * rewrites uc_mcontext.gregs[REG_RIP] to leave a blocking syscall ---- */

static int cancel_pipe[2];

static void *block_in_read(void *arg)
{
    char c;
    read(cancel_pipe[0], &c, 1);
    return (void *)1;
}

TEST_TIMEOUT(cancel_blocked_in_read, 10)
{
    ASSERT_OK(pipe(cancel_pipe));
    pthread_t t;
    ASSERT_EQ(pthread_create(&t, NULL, block_in_read, NULL), 0);
    usleep(50000);
    EXPECT_EQ(pthread_cancel(t), 0);
    void *ret = NULL;
    EXPECT_EQ(pthread_join(t, &ret), 0);
    EXPECT_TRUE(ret == PTHREAD_CANCELED);
}

static void *spin_async(void *arg)
{
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    for (;;)
        __asm__ volatile("" ::: "memory");
    return NULL;
}

TEST_TIMEOUT(cancel_async_spin, 10)
{
    pthread_t t;
    ASSERT_EQ(pthread_create(&t, NULL, spin_async, NULL), 0);
    usleep(50000);
    EXPECT_EQ(pthread_cancel(t), 0);
    void *ret = NULL;
    EXPECT_EQ(pthread_join(t, &ret), 0);
    EXPECT_TRUE(ret == PTHREAD_CANCELED);
}

static void *poll_testcancel(void *arg)
{
    for (;;) {
        pthread_testcancel();
        sched_yield();
    }
    return NULL;
}

/* control: deferred cancel at a testcancel point needs no signal */
TEST_TIMEOUT(cancel_deferred_testcancel, 10)
{
    pthread_t t;
    ASSERT_EQ(pthread_create(&t, NULL, poll_testcancel, NULL), 0);
    usleep(20000);
    EXPECT_EQ(pthread_cancel(t), 0);
    void *ret = NULL;
    EXPECT_EQ(pthread_join(t, &ret), 0);
    EXPECT_TRUE(ret == PTHREAD_CANCELED);
}

static void *spin_until_killed(void *arg)
{
    for (;;)
        pause();
    return NULL;
}

/* A process whose main thread called pthread_exit is still alive; kill(pid)
 * used to fail with ESRCH because the leader is a zombie */
TEST(kill_after_main_pthread_exit)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        pthread_t t;
        if (pthread_create(&t, NULL, spin_until_killed, NULL) != 0)
            _exit(2);
        pthread_exit(NULL);
    }
    usleep(100000);
    EXPECT_OK(kill(pid, 0));
    EXPECT_OK(kill(pid, SIGTERM));
    int st = ktest_wait_timeout(pid, 3);
    if (st == -1) {
        kill(-pid, SIGKILL);
        FAIL("process survived SIGTERM");
        return;
    }
    EXPECT_TRUE(WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);
}

static void *report_tid(void *arg)
{
    int fd = (int)(intptr_t)arg;
    pid_t tid = syscall(SYS_gettid);
    write(fd, &tid, sizeof(tid));
    return NULL;
}

/* waitpid(pid) must reap the process's exited threads with it; they used to
 * stay until a wait(-1), pointing at the freed leader */
TEST(waitpid_pid_reaps_threads)
{
    int fds[2];
    ASSERT_OK(pipe(fds));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        pthread_t t;
        if (pthread_create(&t, NULL, report_tid, (void *)(intptr_t)fds[1]) != 0)
            _exit(2);
        pthread_join(t, NULL);
        _exit(0);
    }
    pid_t tid = 0;
    ASSERT_EQ(read(fds[0], &tid, sizeof(tid)), (ssize_t)sizeof(tid));
    int st = 0;
    ASSERT_EQ(waitpid(pid, &st, 0), pid);
    EXPECT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    struct rlimit rl;
    errno = 0;
    EXPECT_EQ(syscall(SYS_prlimit64, tid, RLIMIT_NOFILE, NULL, &rl), -1);
    EXPECT_EQ(errno, ESRCH);
}

/* ITIMER_REAL belongs to the process, not the thread that set it */
static void *read_itimer(void *arg)
{
    struct itimerval *out = arg;
    getitimer(ITIMER_REAL, out);
    return NULL;
}

static void *clear_itimer(void *arg)
{
    (void)arg;
    struct itimerval off = {0};
    setitimer(ITIMER_REAL, &off, NULL);
    return NULL;
}

TEST(itimer_shared_by_threads)
{
    struct itimerval it = { .it_value = { 30, 0 } }, seen = {0}, now;
    ASSERT_OK(setitimer(ITIMER_REAL, &it, NULL));
    pthread_t t;
    ASSERT_EQ(pthread_create(&t, NULL, read_itimer, &seen), 0);
    pthread_join(t, NULL);
    EXPECT_GE(seen.it_value.tv_sec, 28);

    ASSERT_EQ(pthread_create(&t, NULL, clear_itimer, NULL), 0);
    pthread_join(t, NULL);
    ASSERT_OK(getitimer(ITIMER_REAL, &now));
    EXPECT_EQ(now.it_value.tv_sec, 0);
    EXPECT_EQ(now.it_value.tv_usec, 0);
    alarm(KTEST_DEFAULT_TIMEOUT);
}

static volatile sig_atomic_t alrm_hits;
static void on_alrm(int sig) { (void)sig; alrm_hits++; }

static void *arm_and_exit(void *arg)
{
    (void)arg;
    struct itimerval it = { .it_value = { 0, 100000 } };
    setitimer(ITIMER_REAL, &it, NULL);
    return NULL;
}

/* a timer armed by a thread that then exits still fires for the process */
TEST(itimer_outlives_arming_thread)
{
    alrm_hits = 0;
    signal(SIGALRM, on_alrm);
    pthread_t t;
    ASSERT_EQ(pthread_create(&t, NULL, arm_and_exit, NULL), 0);
    pthread_join(t, NULL);
    for (int i = 0; i < 100 && !alrm_hits; i++)
        usleep(10000);
    EXPECT_EQ(alrm_hits, 1);
    signal(SIGALRM, SIG_DFL);
    alarm(KTEST_DEFAULT_TIMEOUT);
}
