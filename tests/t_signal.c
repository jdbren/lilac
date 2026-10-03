// Signals: delivery, masking, handlers, default actions, interruption.
#include "ktest.h"
#include <sys/time.h>
#include <ucontext.h>
#include <sys/mman.h>
#include <sys/syscall.h>
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

/* kill(-pgid) must reach every member, not just the first 64 */
TEST_TIMEOUT(kill_pgrp_many, 30)
{
    enum { NKIDS = 100 };
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t leader = ASSERT_OK(fork());
    if (leader == 0) {
        setpgid(0, 0);
        for (int i = 0; i < NKIDS; i++) {
            pid_t k = fork();
            if (k < 0)
                _exit(2);
            if (k == 0) {
                write(p[1], "r", 1);
                for (;;)
                    pause();
            }
        }
        write(p[1], "r", 1);
        for (;;)
            pause();
    }
    close(p[1]);
    char c;
    int ready = 0;
    while (ready < NKIDS + 1 && read(p[0], &c, 1) == 1)
        ready++;
    ASSERT_EQ(ready, NKIDS + 1);

    EXPECT_OK(kill(-leader, SIGTERM));
    int st = ktest_wait_timeout(leader, 5);
    EXPECT_TRUE(st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);

    /* every member holds the write end; EOF means all of them died */
    fcntl(p[0], F_SETFL, O_NONBLOCK);
    long long deadline = ktest_now_ns() + 5LL * 1000000000;
    for (;;) {
        ssize_t n = read(p[0], &c, 1);
        if (n == 0)
            break;
        if (ktest_now_ns() > deadline) {
            FAIL("process group members survived kill(-pgid)");
            break;
        }
        usleep(10000);
    }
    close(p[0]);
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

static volatile sig_atomic_t chain_n;

static void chain_usr2(int sig)
{
    chain_n++;
}

static void chain_alrm(int sig)
{
    chain_n++;
    raise(SIGUSR2); /* blocked by sa_mask; delivered on sigreturn's exit */
}

/* A signal delivered on the way out of sigreturn must not "restart"
 * sigreturn itself: the restored ax is pause()'s -EINTR, and an SA_RESTART
 * handler used to rewind ip onto pause's syscall insn with ax = sigreturn. */
TEST(sa_restart_after_sigreturn)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        install(SIGUSR2, chain_usr2, SA_RESTART);
        struct sigaction sa = {0};
        sa.sa_handler = chain_alrm;
        sigemptyset(&sa.sa_mask);
        sigaddset(&sa.sa_mask, SIGUSR2);
        if (sigaction(SIGALRM, &sa, NULL) != 0)
            _exit(2);
        alarm(1);
        errno = 0;
        int r = pause();
        _exit(r == -1 && errno == EINTR && chain_n == 2 ? 0 : 1);
    }
    int st = ktest_wait_timeout(pid, 5);
    if (st == -1)
        FAIL("child hung");
    else if (WIFSIGNALED(st))
        FAIL("child killed by signal %d", WTERMSIG(st));
    else if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
        FAIL("child status %#x (pause result or handler count wrong)", st);
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

/* A stopped task must not run again until SIGCONT */
TEST(sigstop_self_stops_before_returning)
{
    int fds[2];
    ASSERT_OK(pipe(fds));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        close(fds[0]);
        pid_t self = getpid();
        for (int i = 0; i < 20; i++) {
            /* raw kill: musl's raise() makes another syscall after it */
            syscall(SYS_kill, self, SIGSTOP);
            if (write(fds[1], "x", 1) != 1)
                _exit(2);
        }
        _exit(0);
    }
    close(fds[1]);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    for (int i = 0; i < 20; i++) {
        int st = 0;
        ASSERT_EQ(waitpid(pid, &st, WUNTRACED), pid);
        ASSERT_TRUE(WIFSTOPPED(st));
        usleep(20000);
        char c;
        ssize_t n = read(fds[0], &c, 1);
        if (n != -1 || errno != EAGAIN) {
            FAIL("round %d: stopped child still wrote (read=%zd)", i, n);
            kill(pid, SIGKILL);
            ktest_wait_timeout(pid, 3);
            return;
        }
        ASSERT_OK(kill(pid, SIGCONT));
        /* the write lands once the child continues */
        fcntl(fds[0], F_SETFL, 0);
        EXPECT_EQ(read(fds[0], &c, 1), 1);
        fcntl(fds[0], F_SETFL, O_NONBLOCK);
    }
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

/* SIGKILL must end a stopped task without a SIGCONT first */
TEST(sigkill_stopped_task)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        for (;;)
            pause();
    }
    ASSERT_OK(kill(pid, SIGSTOP));
    int st = 0;
    ASSERT_EQ(waitpid(pid, &st, WUNTRACED), pid);
    ASSERT_TRUE(WIFSTOPPED(st));
    ASSERT_OK(kill(pid, SIGKILL));
    st = ktest_wait_timeout(pid, 3);
    EXPECT_TRUE(st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);
    if (st == -1) {
        kill(pid, SIGCONT);
        ktest_wait_timeout(pid, 3);
    }
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

/* Faults at fault_insn (movl (%rax),%eax with rax = 0) and returns eax. */
extern char fault_insn[], fault_insn_end[];
__attribute__((noinline)) static int fault_at_known_rip(const void *addr)
{
    int eax;
    __asm__ volatile(".globl fault_insn, fault_insn_end\n"
                     "fault_insn: movl (%%rax), %%eax\n"
                     "fault_insn_end:"
                     : "=a"(eax) : "a"(addr) : "memory");
    return eax;
}

static volatile unsigned long seen_rip, seen_cr2;

static void skip_fault(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;
    seen_rip = uc->uc_mcontext.gregs[REG_RIP];
    seen_cr2 = uc->uc_mcontext.gregs[REG_CR2];
    uc->uc_mcontext.gregs[REG_RIP] = (unsigned long)fault_insn_end;
    uc->uc_mcontext.gregs[REG_RAX] = 1234;
}

/* The third handler argument is a Linux-layout ucontext_t: gregs hold the
 * interrupted registers, and edits to them take effect on return. */
TEST(ucontext_gregs_read_and_write)
{
    struct sigaction sa = {0};
    sa.sa_sigaction = skip_fault;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    ASSERT_OK(sigaction(SIGSEGV, &sa, NULL));
    int eax = fault_at_known_rip(NULL);
    EXPECT_EQ(seen_rip, (unsigned long)fault_insn);
    EXPECT_EQ(seen_cr2, 0);
    EXPECT_EQ(eax, 1234);
}

static void block_usr2_on_return(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;
    sigaddset(&uc->uc_sigmask, SIGUSR2);
}

TEST(ucontext_sigmask_applied_on_return)
{
    struct sigaction sa = {0};
    sa.sa_sigaction = block_usr2_on_return;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    ASSERT_OK(sigaction(SIGUSR1, &sa, NULL));
    sigset_t cur;
    sigemptyset(&cur);
    ASSERT_OK(sigprocmask(SIG_SETMASK, &cur, NULL));
    raise(SIGUSR1);
    ASSERT_OK(sigprocmask(SIG_BLOCK, NULL, &cur));
    EXPECT_TRUE(sigismember(&cur, SIGUSR2));
    EXPECT_TRUE(!sigismember(&cur, SIGUSR1));
}

static volatile unsigned long seen_uc_flags, seen_link;

static void record_uc_header(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;
    seen_uc_flags = uc->uc_flags;
    seen_link = (unsigned long)uc->uc_link;
    hits = uc->uc_mcontext.fpregs != NULL;
}

TEST(ucontext_header_initialized)
{
    struct sigaction sa = {0};
    sa.sa_sigaction = record_uc_header;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    ASSERT_OK(sigaction(SIGUSR1, &sa, NULL));
    hits = 0;
    seen_uc_flags = seen_link = 0xdead;
    raise(SIGUSR1);
    EXPECT_EQ(seen_uc_flags, 0);
    EXPECT_EQ(seen_link, 0);
    EXPECT_EQ(hits, 1);
}

static volatile unsigned long seen_si_addr;

static void skip_fault_record(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;
    seen_rip = uc->uc_mcontext.gregs[REG_RIP];
    seen_cr2 = uc->uc_mcontext.gregs[REG_CR2];
    seen_si_addr = (unsigned long)si->si_addr;
    uc->uc_mcontext.gregs[REG_RIP] = (unsigned long)fault_insn_end;
}

/* CR2 (and si_addr) carry the faulting address. A nonzero address, since a
 * zeroed frame would make 0 pass by accident. */
TEST(ucontext_cr2_nonzero_fault)
{
    char *p = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_PTR(p);
    struct sigaction sa = {0};
    sa.sa_sigaction = skip_fault_record;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    ASSERT_OK(sigaction(SIGSEGV, &sa, NULL));
    seen_rip = seen_cr2 = seen_si_addr = 0;
    fault_at_known_rip(p + 0x48);
    EXPECT_EQ(seen_rip, (unsigned long)fault_insn);
    EXPECT_EQ(seen_cr2, (unsigned long)(p + 0x48));
    EXPECT_EQ(seen_si_addr, (unsigned long)(p + 0x48));
    munmap(p, 4096);
}

static volatile int zero_bad;
static volatile const char *zero_what;

static bool all_zero(const void *p, size_t n)
{
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++)
        if (b[i])
            return false;
    return true;
}

static void check_zeroed(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;
    zero_bad = 1;
    if (!all_zero(&uc->uc_stack, sizeof(uc->uc_stack)))
        zero_what = "uc_stack";
    else if (uc->uc_mcontext.gregs[REG_ERR] || uc->uc_mcontext.gregs[REG_TRAPNO] ||
             uc->uc_mcontext.gregs[REG_OLDMASK] || uc->uc_mcontext.gregs[REG_CR2])
        zero_what = "err/trapno/oldmask/cr2";
    else if (!all_zero(uc->uc_mcontext.__reserved1, sizeof(uc->uc_mcontext.__reserved1)))
        zero_what = "mcontext reserved";
    else if (si->si_errno != 0)
        zero_what = "si_errno";
    else if (!all_zero((char *)si + 28, 128 - 28))
        zero_what = "siginfo tail";
    else
        zero_bad = 0;
}

__attribute__((noinline)) static void dirty_stack(void)
{
    volatile char junk[16384];
    memset((char *)junk, 0xaa, sizeof(junk));
}

/* Fields the kernel doesn't use must reach the handler as zeros: not stale
 * kernel stack, not whatever was on the user stack. */
TEST(ucontext_fields_zeroed)
{
    struct sigaction sa = {0};
    sa.sa_sigaction = check_zeroed;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    ASSERT_OK(sigaction(SIGUSR1, &sa, NULL));
    zero_bad = -1;
    zero_what = "handler did not run";
    dirty_stack();
    kill(getpid(), SIGUSR1);
    if (zero_bad)
        FAIL("nonzero field: %s", zero_what);
}

/* The kernel's own return trampoline (used when SA_RESTORER is absent;
 * musl always passes one, so go around it). */
struct raw_sigaction {
    void (*handler)(int, siginfo_t *, void *);
    unsigned long flags;
    void (*restorer)(void);
    unsigned long mask;
};

static volatile unsigned long tramp_rip;

static void tramp_handler(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;
    hits++;
    tramp_rip = uc->uc_mcontext.gregs[REG_RIP];
}

TEST(sigtramp_without_restorer)
{
    struct raw_sigaction ka = {
        .handler = tramp_handler,
        .flags = SA_SIGINFO,
        .restorer = NULL,
        .mask = 0,
    };
    ASSERT_OK(syscall(SYS_rt_sigaction, SIGUSR1, &ka, NULL, 8));
    hits = 0;
    tramp_rip = 0;
    for (int i = 0; i < 3; i++)
        EXPECT_OK(kill(getpid(), SIGUSR1));
    EXPECT_EQ(hits, 3);
    EXPECT_NE(tramp_rip, 0);
    /* the interrupted code resumed with its stack intact */
    volatile int canary = 0x5a5a;
    kill(getpid(), SIGUSR1);
    EXPECT_EQ(canary, 0x5a5a);
    EXPECT_EQ(hits, 4);
}

static void block_everything_on_return(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;
    sigfillset(&uc->uc_sigmask);
}

/* sigreturn takes its mask from user memory, and used to let a handler block
 * SIGKILL and SIGSTOP through it, leaving the process unkillable */
TEST(sigreturn_cannot_block_sigkill)
{
    int fds[2];
    ASSERT_OK(pipe(fds));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        struct sigaction sa = {0};
        sa.sa_sigaction = block_everything_on_return;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGUSR1, &sa, NULL) != 0)
            _exit(2);
        raise(SIGUSR1);
        sigset_t cur;
        sigprocmask(SIG_BLOCK, NULL, &cur);
        char ok = sigismember(&cur, SIGKILL) || sigismember(&cur, SIGSTOP) ? 'n' : 'y';
        write(fds[1], &ok, 1);
        for (;;)
            pause();
    }
    close(fds[1]);
    char ok = 0;
    EXPECT_EQ(read(fds[0], &ok, 1), 1);
    EXPECT_EQ(ok, 'y');
    EXPECT_OK(kill(pid, SIGKILL));
    int st = ktest_wait_timeout(pid, 3);
    EXPECT_TRUE(st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);
}

/* A stop and continue with no handler must restart a blocking syscall, not
 * fail it with EINTR (cat used to exit after ^Z and fg) */
TEST(stop_cont_restarts_read)
{
    int fds[2];
    ASSERT_OK(pipe(fds));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        close(fds[1]);
        char c = 0;
        ssize_t r = read(fds[0], &c, 1);
        _exit(r == 1 && c == 'z' ? 0 : r == -1 && errno == EINTR ? 3 : 4);
    }
    close(fds[0]);
    usleep(100000);
    ASSERT_OK(kill(pid, SIGSTOP));
    int st = 0;
    ASSERT_EQ(waitpid(pid, &st, WUNTRACED), pid);
    ASSERT_TRUE(WIFSTOPPED(st));
    ASSERT_OK(kill(pid, SIGCONT));
    usleep(100000);
    EXPECT_EQ(write(fds[1], "z", 1), 1);
    st = ktest_wait_timeout(pid, 3);
    if (st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 3)
        FAIL("read failed with EINTR after SIGCONT");
    EXPECT_TRUE(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* nanosleep resumes after a stop for the time left, as Linux (through
 * restart_syscall), instead of failing or sleeping the full time again */
TEST(stop_cont_resumes_nanosleep)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        struct timespec req = { 1, 500000000 };
        double t0 = now_sec();
        int r = nanosleep(&req, NULL);
        double dt = now_sec() - t0;
        if (r != 0)
            _exit(3);
        _exit(dt < 1.4 ? 4 : dt > 2.1 ? 5 : 0);
    }
    usleep(300000);
    ASSERT_OK(kill(pid, SIGSTOP));
    int st = 0;
    ASSERT_EQ(waitpid(pid, &st, WUNTRACED), pid);
    usleep(500000);
    ASSERT_OK(kill(pid, SIGCONT));
    st = ktest_wait_timeout(pid, 5);
    ASSERT_TRUE(st != -1 && WIFEXITED(st));
    switch (WEXITSTATUS(st)) {
    case 0: break;
    case 3: FAIL("nanosleep failed after SIGCONT"); break;
    case 4: FAIL("nanosleep returned early"); break;
    case 5: FAIL("nanosleep restarted from the full duration"); break;
    default: FAIL("child status %#x", st);
    }
}

/* pause() returns only for a handler: a stop and continue restarts it */
TEST(pause_restarts_after_stop)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        hits = 0;
        install(SIGUSR1, count_handler, 0);
        errno = 0;
        int r = pause();
        _exit(r == -1 && errno == EINTR && hits == 1 ? 0 : 3);
    }
    usleep(100000);
    ASSERT_OK(kill(pid, SIGSTOP));
    int st = 0;
    ASSERT_EQ(waitpid(pid, &st, WUNTRACED), pid);
    ASSERT_OK(kill(pid, SIGCONT));
    usleep(100000);
    ASSERT_OK(kill(pid, SIGUSR1));
    st = ktest_wait_timeout(pid, 3);
    EXPECT_TRUE(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

static unsigned get_mxcsr(void)
{
    unsigned v;
    __asm__ volatile ("stmxcsr %0" : "=m"(v));
    return v;
}

static void set_mxcsr(unsigned v)
{
    __asm__ volatile ("ldmxcsr %0" : : "m"(v));
}

/* sigreturn used to clear MXCSR.DAZ with a hardcoded mask */
TEST(mxcsr_daz_survives_handler)
{
    const unsigned DAZ = 1u << 6;
    unsigned saved = get_mxcsr();
    set_mxcsr(saved | DAZ);
    if (!(get_mxcsr() & DAZ)) {
        set_mxcsr(saved);
        SKIP("cpu has no DAZ");
    }
    hits = 0;
    install(SIGUSR1, count_handler, 0);
    raise(SIGUSR1);
    unsigned after = get_mxcsr();
    set_mxcsr(saved);
    EXPECT_EQ(hits, 1);
    EXPECT_TRUE(after & DAZ);
}
