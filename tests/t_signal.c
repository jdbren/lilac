// Signals: delivery, masking, handlers, default actions, interruption.
#include "ktest.h"
#include <sys/time.h>
#include <time.h>

static volatile sig_atomic_t hits;
static volatile sig_atomic_t last_sig;
static volatile int info_signo, info_code;
static volatile pid_t info_pid;

static void count_handler(int sig)
{
    hits++;
    last_sig = sig;
}

static void info_handler(int sig, siginfo_t *si, void *uc)
{
    hits++;
    info_signo = si->si_signo;
    info_pid = si->si_pid;
    info_code = si->si_code;
}

static void install(int sig, void (*fn)(int), int flags)
{
    struct sigaction sa = {0};
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;
    if (sigaction(sig, &sa, NULL) != 0) {
        FAIL("sigaction(%d) failed errno=%d", sig, errno);
        ktest_abort();
    }
}

static int term_signal_of(pid_t pid)
{
    int st = ktest_wait_timeout(pid, 5);
    if (st == -1)
        return -1;
    return WIFSIGNALED(st) ? WTERMSIG(st) : 0;
}

TEST(handler_runs_on_self_kill)
{
    hits = 0;
    install(SIGUSR1, count_handler, 0);
    EXPECT_OK(kill(getpid(), SIGUSR1));
    EXPECT_EQ(hits, 1);
    EXPECT_EQ(last_sig, SIGUSR1);
}

TEST(raise_delivers)
{
    hits = 0;
    install(SIGUSR2, count_handler, 0);
    EXPECT_EQ(raise(SIGUSR2), 0);
    EXPECT_EQ(hits, 1);
}

TEST(signal_function_api)
{
    hits = 0;
    EXPECT_TRUE(signal(SIGUSR1, count_handler) != SIG_ERR);
    raise(SIGUSR1);
    raise(SIGUSR1);
    EXPECT_EQ(hits, 2); /* handler must stay installed (BSD semantics in musl) */
}

TEST(siginfo_fields)
{
    struct sigaction sa = {0};
    sa.sa_sigaction = info_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    ASSERT_OK(sigaction(SIGUSR1, &sa, NULL));
    hits = 0;
    kill(getpid(), SIGUSR1);
    EXPECT_EQ(hits, 1);
    EXPECT_EQ(info_signo, SIGUSR1);
    EXPECT_EQ(info_pid, getpid());
    EXPECT_EQ(info_code, SI_USER);
}

TEST(sigaction_returns_old)
{
    install(SIGUSR1, count_handler, 0);
    struct sigaction old = {0}, sa = {0};
    sa.sa_handler = SIG_IGN;
    ASSERT_OK(sigaction(SIGUSR1, &sa, &old));
    EXPECT_TRUE(old.sa_handler == count_handler);
    ASSERT_OK(sigaction(SIGUSR1, NULL, &old));
    EXPECT_TRUE(old.sa_handler == SIG_IGN);
}

TEST(sigaction_kill_stop_einval)
{
    struct sigaction sa = {0};
    sa.sa_handler = count_handler;
    EXPECT_ERR(sigaction(SIGKILL, &sa, NULL), EINVAL);
    EXPECT_ERR(sigaction(SIGSTOP, &sa, NULL), EINVAL);
    EXPECT_ERR(sigaction(0, &sa, NULL), EINVAL);
    EXPECT_ERR(sigaction(1000, &sa, NULL), EINVAL);
}

TEST(sigprocmask_block_pending_unblock)
{
    hits = 0;
    install(SIGUSR1, count_handler, 0);
    sigset_t set, old, pend;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    ASSERT_OK(sigprocmask(SIG_BLOCK, &set, &old));
    kill(getpid(), SIGUSR1);
    EXPECT_EQ(hits, 0);
    sigemptyset(&pend);
    EXPECT_OK(sigpending(&pend));
    EXPECT_TRUE(sigismember(&pend, SIGUSR1));
    /* standard signals don't queue: two sends, one delivery */
    kill(getpid(), SIGUSR1);
    ASSERT_OK(sigprocmask(SIG_UNBLOCK, &set, NULL));
    EXPECT_EQ(hits, 1);
    sigpending(&pend);
    EXPECT_TRUE(!sigismember(&pend, SIGUSR1));
}

TEST(sigprocmask_query_and_setmask)
{
    sigset_t set, cur;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR2);
    sigaddset(&set, SIGTERM);
    ASSERT_OK(sigprocmask(SIG_SETMASK, &set, NULL));
    ASSERT_OK(sigprocmask(SIG_SETMASK, NULL, &cur));
    EXPECT_TRUE(sigismember(&cur, SIGUSR2));
    EXPECT_TRUE(sigismember(&cur, SIGTERM));
    EXPECT_TRUE(!sigismember(&cur, SIGUSR1));
    /* SIGKILL can't be blocked */
    sigaddset(&set, SIGKILL);
    sigprocmask(SIG_SETMASK, &set, NULL);
    sigprocmask(SIG_SETMASK, NULL, &cur);
    EXPECT_TRUE(!sigismember(&cur, SIGKILL));
    EXPECT_ERR(sigprocmask(12345, &set, NULL), EINVAL);
}

static volatile int mask_in_handler_had_self;
static void mask_check_handler(int sig)
{
    sigset_t cur;
    sigprocmask(SIG_SETMASK, NULL, &cur);
    mask_in_handler_had_self = sigismember(&cur, sig);
    hits++;
}

TEST(mask_during_and_after_handler)
{
    hits = 0;
    install(SIGUSR1, mask_check_handler, 0);
    raise(SIGUSR1);
    EXPECT_EQ(hits, 1);
    EXPECT_TRUE(mask_in_handler_had_self); /* signal blocked while handling */
    sigset_t cur;
    sigprocmask(SIG_SETMASK, NULL, &cur);
    EXPECT_TRUE(!sigismember(&cur, SIGUSR1)); /* restored after return */
    raise(SIGUSR1);
    EXPECT_EQ(hits, 2);
}

TEST(sa_nodefer)
{
    hits = 0;
    install(SIGUSR1, mask_check_handler, SA_NODEFER);
    raise(SIGUSR1);
    EXPECT_EQ(hits, 1);
    EXPECT_TRUE(!mask_in_handler_had_self);
}

TEST(sa_resethand)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        install(SIGUSR1, count_handler, SA_RESETHAND);
        raise(SIGUSR1); /* handled */
        raise(SIGUSR1); /* default: terminate */
        _exit(0);
    }
    EXPECT_EQ(term_signal_of(pid), SIGUSR1);
}

static volatile int order[4];
static volatile int order_n;
static void order_usr2(int sig) { order[order_n++] = 2; }
static void order_usr1(int sig)
{
    raise(SIGUSR2); /* blocked by sa_mask: must run after we return */
    order[order_n++] = 1;
}

TEST(sa_mask_defers_other_signal)
{
    order_n = 0;
    install(SIGUSR2, order_usr2, 0);
    struct sigaction sa = {0};
    sa.sa_handler = order_usr1;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGUSR2);
    ASSERT_OK(sigaction(SIGUSR1, &sa, NULL));
    raise(SIGUSR1);
    EXPECT_EQ(order_n, 2);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
}

TEST(sig_ign_ignored)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        signal(SIGTERM, SIG_IGN);
        raise(SIGTERM);
        kill(getpid(), SIGTERM);
        _exit(0);
    }
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

TEST(sig_ign_inherited_across_exec)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        signal(SIGTERM, SIG_IGN);
        char *argv[] = { "/tests/helper", "sleep", "1", NULL };
        execv(argv[0], argv);
        _exit(127);
    }
    usleep(200000);
    kill(pid, SIGTERM);
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

TEST(handler_reset_on_exec)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        install(SIGTERM, count_handler, 0);
        char *argv[] = { "/tests/helper", "sleep", "2", NULL };
        execv(argv[0], argv);
        _exit(127);
    }
    usleep(200000);
    kill(pid, SIGTERM);
    EXPECT_EQ(term_signal_of(pid), SIGTERM);
}

TEST(default_actions_terminate)
{
    int sigs[] = { SIGTERM, SIGINT, SIGHUP, SIGUSR1, SIGUSR2, SIGALRM, SIGPIPE, SIGQUIT, SIGABRT };
    for (unsigned i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
        pid_t pid = ASSERT_OK(fork());
        if (pid == 0) {
            for (;;)
                pause();
        }
        usleep(5000);
        kill(pid, sigs[i]);
        int got = term_signal_of(pid);
        if (got != sigs[i])
            FAIL("signal %d: child ended with %d", sigs[i], got);
    }
}

TEST(sigchld_default_ignored)
{
    /* SIGCHLD's default action is ignore: we must not die */
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0)
        _exit(0);
    usleep(20000);
    waitpid(pid, NULL, 0);
    raise(SIGCHLD);
    raise(SIGURG);
    raise(SIGWINCH);
}

TEST(sigchld_on_child_exit)
{
    hits = 0;
    install(SIGCHLD, count_handler, 0);
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0)
        _exit(0);
    for (int i = 0; i < 200 && hits == 0; i++)
        usleep(5000);
    EXPECT_EQ(hits, 1);
    EXPECT_EQ(last_sig, SIGCHLD);
    waitpid(pid, NULL, 0);
}

TEST(sigchld_siginfo)
{
    struct sigaction sa = {0};
    sa.sa_sigaction = info_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    ASSERT_OK(sigaction(SIGCHLD, &sa, NULL));
    hits = 0;
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0)
        _exit(3);
    for (int i = 0; i < 200 && hits == 0; i++)
        usleep(5000);
    EXPECT_EQ(info_signo, SIGCHLD);
    EXPECT_EQ(info_pid, pid);
    EXPECT_EQ(info_code, CLD_EXITED);
    waitpid(pid, NULL, 0);
}

TEST(kill_other_process_handler)
{
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        hits = 0;
        install(SIGUSR1, count_handler, 0);
        write(p[1], "r", 1);
        while (hits == 0)
            pause();
        _exit(42);
    }
    char c;
    read(p[0], &c, 1);
    kill(pid, SIGUSR1);
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 42);
}

TEST(kill_esrch)
{
    EXPECT_ERR(kill(999999, SIGTERM), ESRCH);
}

TEST(kill_sig0_probe)
{
    EXPECT_OK(kill(getpid(), 0));
    EXPECT_ERR(kill(999999, 0), ESRCH);
}

TEST(kill_invalid_signal)
{
    EXPECT_ERR(kill(getpid(), 1000), EINVAL);
    EXPECT_ERR(kill(getpid(), -1), EINVAL);
}

TEST(alarm_delivers_sigalrm)
{
    hits = 0;
    install(SIGALRM, count_handler, 0);
    long long t0 = ktest_now_ns();
    alarm(1);
    while (hits == 0 && ktest_now_ns() - t0 < 3000000000LL)
        pause();
    long long ms = (ktest_now_ns() - t0) / 1000000;
    EXPECT_EQ(hits, 1);
    EXPECT_GE(ms, 900);
    EXPECT_LE(ms, 1500);
}

TEST(alarm_cancel_and_remaining)
{
    hits = 0;
    install(SIGALRM, count_handler, 0);
    alarm(0); /* drop the harness's timeout alarm */
    EXPECT_EQ(alarm(5), 0);
    unsigned left = alarm(0);
    EXPECT_TRUE(left >= 4 && left <= 5);
    usleep(100000);
    EXPECT_EQ(hits, 0);
    alarm(KTEST_DEFAULT_TIMEOUT);
}

TEST(alarm_default_kills)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        alarm(1);
        for (;;)
            pause();
    }
    EXPECT_EQ(term_signal_of(pid), SIGALRM);
}

TEST(setitimer_real)
{
    hits = 0;
    install(SIGALRM, count_handler, 0);
    struct itimerval it = { .it_value = { 0, 200000 } };
    if (setitimer(ITIMER_REAL, &it, NULL) != 0)
        FAIL("setitimer: errno=%d (%s)", errno, strerror(errno));
    for (int i = 0; i < 100 && hits == 0; i++)
        usleep(10000);
    EXPECT_EQ(hits, 1);
}

TEST(pause_returns_eintr)
{
    hits = 0;
    install(SIGALRM, count_handler, 0);
    alarm(1);
    errno = 0;
    int r = pause();
    EXPECT_EQ(r, -1);
    EXPECT_EQ(errno, EINTR);
    EXPECT_EQ(hits, 1);
}

TEST(sigsuspend_atomic_unblock)
{
    hits = 0;
    install(SIGUSR1, count_handler, 0);
    sigset_t block, old, waitmask;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    sigprocmask(SIG_BLOCK, &block, &old);
    pid_t parent = getpid();
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        usleep(50000);
        kill(parent, SIGUSR1);
        _exit(0);
    }
    sigemptyset(&waitmask);
    errno = 0;
    int r = sigsuspend(&waitmask);
    EXPECT_EQ(r, -1);
    EXPECT_EQ(errno, EINTR);
    EXPECT_EQ(hits, 1);
    sigset_t cur;
    sigprocmask(SIG_SETMASK, NULL, &cur);
    EXPECT_TRUE(sigismember(&cur, SIGUSR1)); /* original mask restored */
    waitpid(pid, NULL, 0);
}

TEST(read_interrupted_eintr)
{
    hits = 0;
    install(SIGALRM, count_handler, 0); /* no SA_RESTART */
    int p[2];
    ASSERT_OK(pipe(p));
    alarm(1);
    char c;
    errno = 0;
    ssize_t r = read(p[0], &c, 1);
    EXPECT_EQ(r, -1);
    EXPECT_EQ(errno, EINTR);
    EXPECT_EQ(hits, 1);
}

TEST(read_restarted_sa_restart)
{
    hits = 0;
    install(SIGALRM, count_handler, SA_RESTART);
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        sleep(2);
        write(p[1], "z", 1);
        _exit(0);
    }
    alarm(1);
    char c = 0;
    ssize_t r = read(p[0], &c, 1);
    EXPECT_EQ(r, 1);
    EXPECT_EQ(c, 'z');
    EXPECT_EQ(hits, 1);
    waitpid(pid, NULL, 0);
}

TEST(wait_interrupted_eintr)
{
    hits = 0;
    install(SIGALRM, count_handler, 0);
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        sleep(3);
        _exit(0);
    }
    alarm(1);
    errno = 0;
    pid_t r = waitpid(pid, NULL, 0);
    EXPECT_EQ(r, -1);
    EXPECT_EQ(errno, EINTR);
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

TEST(nanosleep_interrupted_remaining)
{
    hits = 0;
    install(SIGALRM, count_handler, 0);
    alarm(1);
    struct timespec req = { 3, 0 }, rem = { 0, 0 };
    errno = 0;
    int r = nanosleep(&req, &rem);
    EXPECT_EQ(r, -1);
    EXPECT_EQ(errno, EINTR);
    EXPECT_TRUE(rem.tv_sec >= 1 && rem.tv_sec <= 2);
}

static volatile sig_atomic_t depth_max, depth_cur;
static void nested_handler(int sig)
{
    depth_cur++;
    if (depth_cur > depth_max)
        depth_max = depth_cur;
    if (sig == SIGUSR1)
        raise(SIGUSR2);
    depth_cur--;
}

TEST(nested_handlers)
{
    depth_max = depth_cur = 0;
    install(SIGUSR1, nested_handler, 0);
    install(SIGUSR2, nested_handler, 0);
    raise(SIGUSR1);
    EXPECT_EQ(depth_max, 2);
    EXPECT_EQ(depth_cur, 0);
}

static void clobber_handler(int sig)
{
    /* Use lots of registers / FP state; the interrupted code must not notice */
    volatile double d = 1.0;
    for (int i = 0; i < 100; i++)
        d = d * 1.5 + i;
    hits++;
}

TEST(registers_preserved_across_handler)
{
    hits = 0;
    install(SIGALRM, clobber_handler, 0);
    struct itimerval it = { .it_interval = { 0, 1000 }, .it_value = { 0, 1000 } };
    int have_itimer = setitimer(ITIMER_REAL, &it, NULL) == 0;
    if (!have_itimer)
        alarm(1);
    double acc = 0;
    unsigned long iacc = 0;
    long long t0 = ktest_now_ns();
    for (unsigned long i = 0; ktest_now_ns() - t0 < 1500000000LL; i++) {
        acc += (double)i * 0.5;
        iacc += i;
        if (i % 100000 == 0 && acc != (double)iacc * 0.5) {
            FAIL("FP state corrupted at i=%lu", i);
            break;
        }
    }
    struct itimerval off = {0};
    setitimer(ITIMER_REAL, &off, NULL);
    alarm(0);
    EXPECT_GT(hits, 0);
}

static int do_div0(void *arg)
{
    volatile int a = 1, b = 0;
    return a / b;
}

static int do_ud2(void *arg)
{
    __asm__ volatile("ud2");
    return 0;
}

static int do_int3(void *arg)
{
    __asm__ volatile("int3");
    return 0;
}

static int do_abort(void *arg)
{
    abort();
}

TEST(fault_signals)
{
    struct { int (*fn)(void *); int sig; const char *name; } cases[] = {
        { do_div0, SIGFPE, "div0" },
        { do_ud2, SIGILL, "ud2" },
        { do_int3, SIGTRAP, "int3" },
        { do_abort, SIGABRT, "abort" },
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int st = ktest_run_child(cases[i].fn, NULL);
        if (st == -1 || !WIFSIGNALED(st) || WTERMSIG(st) != cases[i].sig)
            FAIL("%s: expected signal %d, status=0x%x", cases[i].name, cases[i].sig, st);
    }
}

static volatile void *fault_addr;
static void segv_handler(int sig, siginfo_t *si, void *uc)
{
    fault_addr = si->si_addr;
    _exit(si->si_signo == SIGSEGV ? 0 : 1);
}

static int segv_caught(void *arg)
{
    struct sigaction sa = {0};
    sa.sa_sigaction = segv_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    volatile uintptr_t addr = 0x10;
    *(volatile int *)addr = 1;
    return 2;
}

TEST(segv_handler_catches)
{
    int st = ktest_run_child(segv_caught, NULL);
    EXPECT_TRUE(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0);
    if (st != -1 && WIFSIGNALED(st))
        FAIL("child died with signal %d instead of running handler", WTERMSIG(st));
}

static int p_addr[2];
static void segv_addr_handler(int sig, siginfo_t *si, void *uc)
{
    void *a = si->si_addr;
    write(p_addr[1], &a, sizeof(a));
    _exit(0);
}

TEST(segv_si_addr)
{
    ASSERT_OK(pipe(p_addr));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        struct sigaction sa = {0};
        sa.sa_sigaction = segv_addr_handler;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &sa, NULL);
        *(volatile int *)0x1238 = 1;
        _exit(2);
    }
    close(p_addr[1]);
    void *a = NULL;
    EXPECT_EQ(ktest_read_all(p_addr[0], &a, sizeof(a)), sizeof(a));
    EXPECT_EQ((uintptr_t)a, 0x1238);
    waitpid(pid, NULL, 0);
}

TEST(blocked_sync_fault_still_kills)
{
    /* a synchronous SIGSEGV while SIGSEGV is blocked must still kill */
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        sigset_t s;
        sigemptyset(&s);
        sigaddset(&s, SIGSEGV);
        sigprocmask(SIG_BLOCK, &s, NULL);
        *(volatile int *)0 = 1;
        _exit(0);
    }
    EXPECT_EQ(term_signal_of(pid), SIGSEGV);
}

TEST(sigstop_sigcont)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        for (;;)
            pause();
    }
    usleep(10000);
    EXPECT_OK(kill(pid, SIGSTOP));
    int st = 0;
    pid_t r = waitpid(pid, &st, WUNTRACED);
    EXPECT_EQ(r, pid);
    EXPECT_TRUE(WIFSTOPPED(st));
    EXPECT_OK(kill(pid, SIGCONT));
    kill(pid, SIGKILL);
    st = ktest_wait_timeout(pid, 3);
    EXPECT_TRUE(st != -1 && WIFSIGNALED(st));
}

TEST(pending_cleared_on_fork)
{
    sigset_t s, pend;
    sigemptyset(&s);
    sigaddset(&s, SIGUSR1);
    sigprocmask(SIG_BLOCK, &s, NULL);
    raise(SIGUSR1);
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        sigpending(&pend);
        /* child: pending set is empty, mask is inherited */
        sigset_t m;
        sigprocmask(SIG_SETMASK, NULL, &m);
        _exit(sigismember(&pend, SIGUSR1) ? 1 : !sigismember(&m, SIGUSR1) ? 2 : 0);
    }
    int st;
    waitpid(pid, &st, 0);
    EXPECT_EQ(WEXITSTATUS(st), 0);
    signal(SIGUSR1, SIG_IGN);
    sigprocmask(SIG_UNBLOCK, &s, NULL);
}
