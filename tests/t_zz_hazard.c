// Probes that may crash or wedge the kernel. Run last so a panic here does not
// hide the results of every other suite.
#include "ktest.h"
#include <pthread.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <ucontext.h>
#include <sys/syscall.h>

static long raw_syscall0(long nr)
{
    long ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(nr) : "rcx", "r11", "memory");
    return ret;
}

TEST(negative_syscall_number)
{
    /* entry_64.S range-checks with a signed compare; a negative number
       would index before the syscall table. */
    EXPECT_EQ(raw_syscall0(-1), -ENOSYS);
    EXPECT_EQ(raw_syscall0(-100), -ENOSYS);
    EXPECT_EQ(raw_syscall0((long)0x8000000000000000UL), -ENOSYS);
}

TEST(huge_syscall_number)
{
    EXPECT_EQ(raw_syscall0(0x7fffffffffffffffL), -ENOSYS);
    EXPECT_EQ(raw_syscall0(0x100000000L), -ENOSYS);
}

TEST(sigreturn_garbage_frame)
{
    /* calling rt_sigreturn with no signal frame must kill the process,
       not the kernel */
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        raw_syscall0(SYS_rt_sigreturn);
        _exit(0);
    }
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(st != -1);
    EXPECT_TRUE(WIFSIGNALED(st));
}

TEST(sigreturn_kernel_rip)
{
    /* craft a frame whose saved rip points into the kernel */
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        unsigned long frame[64];
        for (int i = 0; i < 64; i++)
            frame[i] = 0xffffffff80000000UL;
        __asm__ volatile("mov %0, %%rsp\n\tmov %1, %%eax\n\tsyscall"
                         :: "r"(frame), "i"(SYS_rt_sigreturn) : "memory");
        _exit(0);
    }
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(st != -1);
    EXPECT_TRUE(WIFSIGNALED(st));
}

TEST_TIMEOUT(fork_bomb_bounded, 60)
{
    /* create processes until fork fails (or 500), then reap them all.
       The kernel should return EAGAIN/ENOMEM rather than panic. */
    pid_t pids[500];
    int n = 0;
    int err = 0;
    for (; n < 500; n++) {
        pids[n] = fork();
        if (pids[n] < 0) {
            err = errno;
            break;
        }
        if (pids[n] == 0) {
            pause();
            _exit(0);
        }
    }
    ktest_log("created %d processes (stopped with errno=%d)", n, err);
    for (int i = 0; i < n; i++)
        kill(pids[i], SIGKILL);
    for (int i = 0; i < n; i++)
        waitpid(pids[i], NULL, 0);
    /* system still works */
    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0)
        _exit(0);
    waitpid(pid, NULL, 0);
}

TEST_TIMEOUT(oom_anon_memory, 60)
{
    /* touch memory until mmap fails or we're killed; must not panic */
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        for (int i = 0; i < 1024; i++) {
            char *p = mmap(NULL, 1 << 20, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED)
                _exit(0);
            for (int j = 0; j < (1 << 20); j += 4096)
                p[j] = 1;
        }
        _exit(1); /* 1GB on a 256MB machine? */
    }
    int st = ktest_wait_timeout(pid, 50);
    EXPECT_TRUE(st != -1);
    ktest_log("oom child status 0x%x", st);
    /* system still works */
    pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0)
        _exit(0);
    waitpid(pid, NULL, 0);
}

static int leader_pipe_w;

static void *noop_thread(void *arg)
{
    return arg;
}

static void *outlive_leader(void *arg)
{
    usleep(300000);
    /* both of these read the (possibly freed) group leader */
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0)
        _exit(3);
    pthread_t t;
    if (pthread_create(&t, NULL, noop_thread, NULL) != 0 || pthread_join(t, NULL) != 0)
        _exit(4);
    write(leader_pipe_w, "m", 1);
    exit(7);
}

/* The leader's pthread_exit must not make the process waitable while another
 * thread still runs: the parent used to reap (free) the leader, and the
 * survivor then read freed memory through tg_leader. */
TEST_TIMEOUT(leader_reaped_with_live_threads, 20)
{
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        close(p[0]);
        leader_pipe_w = p[1];
        pthread_t t;
        if (pthread_create(&t, NULL, outlive_leader, NULL) != 0)
            _exit(2);
        pthread_exit(NULL);
    }
    close(p[1]);
    int st = ktest_wait_timeout(pid, 10);
    fcntl(p[0], F_SETFL, O_NONBLOCK);
    char c = 0;
    ssize_t n = read(p[0], &c, 1);
    close(p[0]);
    EXPECT_TRUE(n == 1 && c == 'm'); /* surviving thread finished before wait returned */
    EXPECT_TRUE(st != -1 && WIFEXITED(st));
    if (st != -1 && WIFEXITED(st))
        EXPECT_EQ(WEXITSTATUS(st), 7);
}

/* ---- sigreturn through a ucontext the handler edited ---- */

#define EFL_TF   0x100UL
#define EFL_IF   0x200UL
#define EFL_DF   0x400UL
#define EFL_IOPL 0x3000UL

/* Faults at hz_fault (load from addr in rax); on resume at hz_resume the
 * flags are captured before anything can change them, then DF is cleared. */
extern char hz_fault[], hz_resume[];
__attribute__((noinline)) static unsigned long hz_fault_flags(const void *addr)
{
    unsigned long flags;
    __asm__ volatile(".globl hz_fault, hz_resume\n"
                     "hz_fault: movl (%%rax), %%eax\n"
                     "hz_resume: pushfq\n"
                     "popq %%rdx\n"
                     "cld\n"
                     : "=d"(flags), "+a"(addr) : : "memory");
    return flags;
}

typedef void (*uc_edit_fn)(ucontext_t *uc);
static uc_edit_fn hz_edit;

static void hz_segv(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;
    uc->uc_mcontext.gregs[REG_RIP] = (unsigned long)hz_resume;
    if (hz_edit)
        hz_edit(uc);
}

static void hz_install_segv(uc_edit_fn edit)
{
    hz_edit = edit;
    struct sigaction sa = {0};
    sa.sa_sigaction = hz_segv;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, NULL) != 0)
        _exit(90);
}

static void edit_flags(ucontext_t *uc)
{
    unsigned long f = uc->uc_mcontext.gregs[REG_EFL];
    f |= EFL_DF | EFL_IOPL;   /* DF: a handler may set it; IOPL: may not */
    f &= ~EFL_IF;             /* nor may it disable interrupts */
    uc->uc_mcontext.gregs[REG_EFL] = f;
}

/* Only the user-changeable flags come back from the frame */
TEST(sigreturn_flags_filtered)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        hz_install_segv(edit_flags);
        unsigned long f = hz_fault_flags(NULL);
        int bad = 0;
        if (!(f & EFL_DF))
            bad |= 1;   /* allowed edit was lost */
        if (f & EFL_IOPL)
            bad |= 2;
        if (!(f & EFL_IF))
            bad |= 4;
        _exit(bad);
    }
    int st = ktest_wait_timeout(pid, 5);
    ASSERT_TRUE(st != -1 && WIFEXITED(st));
    int bad = WEXITSTATUS(st);
    if (bad & 1)
        FAIL("DF set by the handler was not restored");
    if (bad & 2)
        FAIL("handler raised IOPL");
    if (bad & 4)
        FAIL("handler cleared IF");
    EXPECT_EQ(bad & ~7, 0);
}

static volatile int trap_hits;

static void edit_tf(ucontext_t *uc)
{
    uc->uc_mcontext.gregs[REG_EFL] |= EFL_TF;
}

static void on_trap(int sig, siginfo_t *si, void *ucv)
{
    ucontext_t *uc = ucv;
    trap_hits++;
    uc->uc_mcontext.gregs[REG_EFL] &= ~EFL_TF; /* one step is enough */
}

/* TF from the frame single-steps the resumed code: the step raises SIGTRAP,
 * whose handler must itself start with TF clear. */
TEST(sigreturn_trap_flag_single_steps)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        struct sigaction sa = {0};
        sa.sa_sigaction = on_trap;
        sa.sa_flags = SA_SIGINFO;
        sigemptyset(&sa.sa_mask);
        if (sigaction(SIGTRAP, &sa, NULL) != 0)
            _exit(90);
        hz_install_segv(edit_tf);
        hz_fault_flags(NULL);
        _exit(trap_hits == 1 ? 0 : 10 + (trap_hits > 9 ? 9 : trap_hits));
    }
    int st = ktest_wait_timeout(pid, 5);
    if (st == -1)
        FAIL("hung (single-step trap never delivered as SIGTRAP?)");
    else if (WIFSIGNALED(st))
        FAIL("killed by signal %d", WTERMSIG(st));
    else if (WEXITSTATUS(st) != 0)
        FAIL("expected 1 SIGTRAP, got %d", WEXITSTATUS(st) - 10);
}

static void edit_kernel_rip(ucontext_t *uc)
{
    uc->uc_mcontext.gregs[REG_RIP] = 0xffffffff80000000UL;
}

static void edit_noncanonical_rip(ucontext_t *uc)
{
    uc->uc_mcontext.gregs[REG_RIP] = 0x0000800000000000UL;
}

static void edit_kernel_rsp(ucontext_t *uc)
{
    uc->uc_mcontext.gregs[REG_RSP] = 0xffff900000000000UL;
}

static void edit_bad_fpregs(ucontext_t *uc)
{
    uc->uc_mcontext.fpregs = (void *)0xffffffff80000000UL;
}

static int status_after_edit(uc_edit_fn edit)
{
    pid_t pid = fork();
    if (pid < 0)
        return -2;
    if (pid == 0) {
        hz_install_segv(edit);
        hz_fault_flags(NULL);
        _exit(0);
    }
    return ktest_wait_timeout(pid, 5);
}

static void expect_segv_kill(uc_edit_fn edit, const char *what)
{
    int st = status_after_edit(edit);
    if (st == -1)
        FAIL("%s: child hung", what);
    else if (!WIFSIGNALED(st) || WTERMSIG(st) != SIGSEGV)
        FAIL("%s: expected death by SIGSEGV, status %#x", what, st);
}

/* A handler can point the resumed context anywhere; the kernel must refuse
 * kernel or non-canonical addresses by killing the process. */
TEST(sigreturn_bad_regs_from_ucontext)
{
    expect_segv_kill(edit_kernel_rip, "kernel rip");
    expect_segv_kill(edit_noncanonical_rip, "non-canonical rip");
    expect_segv_kill(edit_kernel_rsp, "kernel rsp");
    expect_segv_kill(edit_bad_fpregs, "kernel fpregs pointer");
}

static void edit_null_fpregs(ucontext_t *uc)
{
    uc->uc_mcontext.fpregs = NULL;
}

/* As on Linux, fpregs == NULL means "no saved FP state": resume with the
 * initial FP state rather than killing the process. */
TEST(sigreturn_null_fpregs_resets_fp)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        hz_install_segv(edit_null_fpregs);
        unsigned int mxcsr = 0x3f80; /* default | round toward zero */
        __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
        hz_fault_flags(NULL);
        __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
        _exit(mxcsr == 0x1f80 ? 0 : 1);
    }
    int st = ktest_wait_timeout(pid, 5);
    if (st == -1)
        FAIL("child hung");
    else if (WIFSIGNALED(st))
        FAIL("killed by signal %d", WTERMSIG(st));
    else if (WEXITSTATUS(st) != 0)
        FAIL("MXCSR not reset to the initial state");
}

static volatile sig_atomic_t hz_alrm;
static void hz_on_alrm(int sig) { (void)sig; hz_alrm++; }

TEST(itimer_huge_interval)
{
    hz_alrm = 0;
    signal(SIGALRM, hz_on_alrm);
    struct itimerval it = {
        .it_interval = { 9223372037LL, 0 },
        .it_value = { 0, 1000 },
    }, cur;
    ASSERT_OK(setitimer(ITIMER_REAL, &it, NULL));
    for (int i = 0; i < 50 && !hz_alrm; i++)
        usleep(10000);
    usleep(50000);
    EXPECT_EQ(hz_alrm, 1);
    ASSERT_OK(getitimer(ITIMER_REAL, &cur));
    EXPECT_GT(cur.it_interval.tv_sec, 9000000000LL);
    struct itimerval off = {0};
    setitimer(ITIMER_REAL, &off, NULL);
    signal(SIGALRM, SIG_DFL);
    alarm(KTEST_DEFAULT_TIMEOUT);
}
