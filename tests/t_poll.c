// poll/ppoll/select/pselect: readiness, timeouts, wakeups, bad fds, signals.
#include "ktest.h"
#include <poll.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <termios.h>

#define MS 1000000LL

static volatile sig_atomic_t hits;

static void count_handler(int sig)
{
    hits++;
}

static void install(int sig, int flags)
{
    struct sigaction sa = {0};
    sa.sa_handler = count_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;
    if (sigaction(sig, &sa, NULL) != 0) {
        FAIL("sigaction(%d) failed errno=%d", sig, errno);
        ktest_abort();
    }
}

/* Fork a child that sleeps ms, then writes one byte to fd and exits. */
static pid_t delayed_write(int fd, int ms)
{
    pid_t pid = fork();
    if (pid == 0) {
        usleep(ms * 1000);
        write(fd, "x", 1);
        _exit(0);
    }
    return pid;
}

/* Fork a child that sleeps ms, then sends sig to the parent. */
static pid_t delayed_kill(int sig, int ms)
{
    pid_t parent = getpid();
    pid_t pid = fork();
    if (pid == 0) {
        usleep(ms * 1000);
        kill(parent, sig);
        _exit(0);
    }
    return pid;
}

/* Fork a child that stops and continues the parent every period_ms for
 * total_ms. Each stop/continue restarts the parent's syscall with no handler. */
static pid_t stop_cont_storm(int period_ms, int total_ms)
{
    pid_t parent = getpid();
    pid_t pid = fork();
    if (pid == 0) {
        for (int t = 0; t < total_ms; t += period_ms) {
            usleep(period_ms * 1000);
            kill(parent, SIGSTOP);
            usleep(10 * 1000);
            kill(parent, SIGCONT);
        }
        _exit(0);
    }
    return pid;
}

static int open_tmpfile(void)
{
    char path[64];
    snprintf(path, sizeof(path), "/tmp/t_poll.%d", getpid());
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    unlink(path);
    return fd;
}

/* ---------------- poll ---------------- */

TEST(poll_empty_pipe_not_readable)
{
    int p[2];
    ASSERT_OK(pipe(p));
    struct pollfd pfd = { .fd = p[0], .events = POLLIN };
    EXPECT_EQ(poll(&pfd, 1, 0), 0);
    EXPECT_EQ(pfd.revents, 0);
}

TEST(poll_write_end_writable)
{
    int p[2];
    ASSERT_OK(pipe(p));
    struct pollfd pfd = { .fd = p[1], .events = POLLOUT };
    EXPECT_EQ(poll(&pfd, 1, 0), 1);
    EXPECT_TRUE(pfd.revents & POLLOUT);
}

TEST(poll_full_pipe_not_writable)
{
    int p[2];
    ASSERT_OK(pipe2(p, O_NONBLOCK));
    char buf[512];
    memset(buf, 'f', sizeof(buf));
    long total = 0;
    for (;;) {
        ssize_t n = write(p[1], buf, sizeof(buf));
        if (n < 0)
            break;
        total += n;
        if (total > (1 << 20)) {
            FAIL("pipe never filled");
            ktest_abort();
        }
    }
    EXPECT_EQ(errno, EAGAIN);

    struct pollfd pfd = { .fd = p[1], .events = POLLOUT };
    EXPECT_EQ(poll(&pfd, 1, 0), 0);
    EXPECT_EQ(pfd.revents & POLLOUT, 0);

    EXPECT_EQ(read(p[0], buf, sizeof(buf)), (ssize_t)sizeof(buf));
    EXPECT_EQ(poll(&pfd, 1, 0), 1);
    EXPECT_TRUE(pfd.revents & POLLOUT);
}

TEST(poll_hup_when_writer_closed)
{
    int p[2];
    ASSERT_OK(pipe(p));
    close(p[1]);
    struct pollfd pfd = { .fd = p[0], .events = POLLIN };
    EXPECT_EQ(poll(&pfd, 1, 0), 1);
    EXPECT_TRUE(pfd.revents & POLLHUP);
}

TEST(poll_err_when_reader_closed)
{
    signal(SIGPIPE, SIG_IGN);
    int p[2];
    ASSERT_OK(pipe(p));
    close(p[0]);
    /* POLLERR is reported even if not requested */
    struct pollfd pfd = { .fd = p[1], .events = 0 };
    EXPECT_EQ(poll(&pfd, 1, 0), 1);
    EXPECT_TRUE(pfd.revents & POLLERR);
}

TEST(poll_only_requested_events)
{
    int p[2];
    ASSERT_OK(pipe(p));
    write(p[1], "x", 1);
    /* readable, but only POLLOUT asked for on the read end */
    struct pollfd pfd = { .fd = p[0], .events = POLLOUT };
    EXPECT_EQ(poll(&pfd, 1, 0), 0);
    EXPECT_EQ(pfd.revents & POLLIN, 0);
}

TEST(poll_counts_ready_fds)
{
    int a[2], b[2], c[2];
    ASSERT_OK(pipe(a));
    ASSERT_OK(pipe(b));
    ASSERT_OK(pipe(c));
    write(a[1], "x", 1);
    write(c[1], "x", 1);
    struct pollfd pfd[3] = {
        { .fd = a[0], .events = POLLIN },
        { .fd = b[0], .events = POLLIN },
        { .fd = c[0], .events = POLLIN },
    };
    EXPECT_EQ(poll(pfd, 3, 0), 2);
    EXPECT_TRUE(pfd[0].revents & POLLIN);
    EXPECT_EQ(pfd[1].revents, 0);
    EXPECT_TRUE(pfd[2].revents & POLLIN);
}

TEST(poll_same_fd_twice)
{
    int p[2];
    ASSERT_OK(pipe(p));
    write(p[1], "x", 1);
    struct pollfd pfd[2] = {
        { .fd = p[0], .events = POLLIN },
        { .fd = p[0], .events = POLLIN },
    };
    EXPECT_EQ(poll(pfd, 2, 0), 2);
}

TEST(poll_negative_fd_ignored)
{
    int p[2];
    ASSERT_OK(pipe(p));
    write(p[1], "x", 1);
    struct pollfd pfd[2] = {
        { .fd = -1, .events = POLLIN, .revents = 0x7 },
        { .fd = p[0], .events = POLLIN },
    };
    EXPECT_EQ(poll(pfd, 2, 0), 1);
    EXPECT_EQ(pfd[0].revents, 0);
}

TEST(poll_closed_fd_nval)
{
    int p[2];
    ASSERT_OK(pipe(p));
    close(p[0]);
    struct pollfd pfd = { .fd = p[0], .events = POLLIN };
    EXPECT_EQ(poll(&pfd, 1, 0), 1);
    EXPECT_TRUE(pfd.revents & POLLNVAL);
}

TEST(poll_regular_file_ready)
{
    int fd = open_tmpfile();
    ASSERT_GE(fd, 0);
    struct pollfd pfd = { .fd = fd, .events = POLLIN | POLLOUT };
    EXPECT_EQ(poll(&pfd, 1, 0), 1);
    EXPECT_TRUE(pfd.revents & POLLIN);
    EXPECT_TRUE(pfd.revents & POLLOUT);
    close(fd);
}

TEST(poll_bad_pointer_efault)
{
    EXPECT_ERR(poll((struct pollfd *)8, 1, 0), EFAULT);
}

TEST(poll_too_many_fds_einval)
{
    EXPECT_ERR(syscall(SYS_poll, NULL, 1u << 20, 0), EINVAL);
}

TEST(poll_timeout_elapses)
{
    int p[2];
    ASSERT_OK(pipe(p));
    struct pollfd pfd = { .fd = p[0], .events = POLLIN };
    long long t0 = ktest_now_ns();
    EXPECT_EQ(poll(&pfd, 1, 100), 0);
    long long dt = ktest_now_ns() - t0;
    EXPECT_GE(dt, 90 * MS);
    EXPECT_LT(dt, 2000 * MS);
}

TEST(poll_no_fds_sleeps)
{
    long long t0 = ktest_now_ns();
    EXPECT_EQ(poll(NULL, 0, 100), 0);
    long long dt = ktest_now_ns() - t0;
    EXPECT_GE(dt, 90 * MS);
    EXPECT_LT(dt, 2000 * MS);
}

TEST(poll_wakes_on_write)
{
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = delayed_write(p[1], 100);
    ASSERT_GE(pid, 0);
    struct pollfd pfd = { .fd = p[0], .events = POLLIN };
    long long t0 = ktest_now_ns();
    EXPECT_EQ(poll(&pfd, 1, -1), 1);
    long long dt = ktest_now_ns() - t0;
    EXPECT_TRUE(pfd.revents & POLLIN);
    EXPECT_GE(dt, 50 * MS);
    waitpid(pid, NULL, 0);
}

TEST(poll_wakes_on_writer_close)
{
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        usleep(100000);
        _exit(0);       /* drops the child's write end */
    }
    close(p[1]);
    struct pollfd pfd = { .fd = p[0], .events = POLLIN };
    EXPECT_EQ(poll(&pfd, 1, 5000), 1);
    EXPECT_TRUE(pfd.revents & POLLHUP);
    waitpid(pid, NULL, 0);
}

TEST(poll_wakes_on_reader_drain)
{
    int p[2];
    ASSERT_OK(pipe2(p, O_NONBLOCK));
    char buf[512];
    memset(buf, 'f', sizeof(buf));
    while (write(p[1], buf, sizeof(buf)) > 0)
        ;
    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        usleep(100000);
        read(p[0], buf, sizeof(buf));
        _exit(0);
    }
    struct pollfd pfd = { .fd = p[1], .events = POLLOUT };
    EXPECT_EQ(poll(&pfd, 1, 5000), 1);
    EXPECT_TRUE(pfd.revents & POLLOUT);
    waitpid(pid, NULL, 0);
}

TEST(poll_wakes_one_of_many)
{
    int fds[16][2];
    struct pollfd pfd[16];
    for (int i = 0; i < 16; i++) {
        ASSERT_OK(pipe(fds[i]));
        pfd[i] = (struct pollfd){ .fd = fds[i][0], .events = POLLIN };
    }
    pid_t pid = delayed_write(fds[11][1], 100);
    ASSERT_GE(pid, 0);
    EXPECT_EQ(poll(pfd, 16, 5000), 1);
    for (int i = 0; i < 16; i++)
        EXPECT_EQ(!!(pfd[i].revents & POLLIN), i == 11);
    waitpid(pid, NULL, 0);
}

/* Waiters left queued after poll returns would be woken through a dead
 * stack frame by the later writes. */
TEST(poll_no_stale_waiters)
{
    int p[2];
    ASSERT_OK(pipe(p));
    struct pollfd pfd = { .fd = p[0], .events = POLLIN };
    char c;
    for (int i = 0; i < 200; i++) {
        EXPECT_EQ(poll(&pfd, 1, 1), 0);
        write(p[1], "x", 1);
        EXPECT_EQ(poll(&pfd, 1, 0), 1);
        read(p[0], &c, 1);
    }
}

TEST(poll_interrupted_by_signal)
{
    install(SIGUSR1, 0);
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = delayed_kill(SIGUSR1, 100);
    ASSERT_GE(pid, 0);
    struct pollfd pfd = { .fd = p[0], .events = POLLIN };
    EXPECT_ERR(poll(&pfd, 1, 5000), EINTR);
    EXPECT_EQ(hits, 1);
    waitpid(pid, NULL, 0);
}

/* poll is never restarted, even with SA_RESTART */
TEST(poll_eintr_despite_sa_restart)
{
    install(SIGUSR1, SA_RESTART);
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = delayed_kill(SIGUSR1, 100);
    ASSERT_GE(pid, 0);
    struct pollfd pfd = { .fd = p[0], .events = POLLIN };
    EXPECT_ERR(poll(&pfd, 1, 5000), EINTR);
    EXPECT_EQ(hits, 1);
    waitpid(pid, NULL, 0);
}

/* A restart must keep the original deadline, not re-arm the full timeout. */
TEST(poll_restart_keeps_deadline)
{
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = stop_cont_storm(100, 1500);
    ASSERT_GE(pid, 0);
    struct pollfd pfd = { .fd = p[0], .events = POLLIN };
    long long t0 = ktest_now_ns();
    EXPECT_EQ(poll(&pfd, 1, 300), 0);
    long long dt = ktest_now_ns() - t0;
    EXPECT_GE(dt, 290 * MS);
    EXPECT_LT(dt, 1000 * MS);
    waitpid(pid, NULL, 0);
}

TEST(poll_restart_no_timeout)
{
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t storm = stop_cont_storm(100, 500);
    ASSERT_GE(storm, 0);
    pid_t pid = delayed_write(p[1], 700);
    ASSERT_GE(pid, 0);
    struct pollfd pfd = { .fd = p[0], .events = POLLIN };
    EXPECT_EQ(poll(&pfd, 1, -1), 1);
    EXPECT_TRUE(pfd.revents & POLLIN);
    waitpid(storm, NULL, 0);
    waitpid(pid, NULL, 0);
}

/* ---------------- ppoll ---------------- */

TEST(ppoll_zero_timeout)
{
    int p[2];
    ASSERT_OK(pipe(p));
    struct pollfd pfd = { .fd = p[0], .events = POLLIN };
    struct timespec ts = { 0, 0 };
    EXPECT_EQ(ppoll(&pfd, 1, &ts, NULL), 0);
}

TEST(ppoll_timeout_elapses)
{
    int p[2];
    ASSERT_OK(pipe(p));
    struct pollfd pfd = { .fd = p[0], .events = POLLIN };
    struct timespec ts = { 0, 100 * MS };
    long long t0 = ktest_now_ns();
    EXPECT_EQ(ppoll(&pfd, 1, &ts, NULL), 0);
    long long dt = ktest_now_ns() - t0;
    EXPECT_GE(dt, 90 * MS);
    EXPECT_LT(dt, 2000 * MS);
}

TEST(ppoll_restart_keeps_deadline)
{
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = stop_cont_storm(100, 1500);
    ASSERT_GE(pid, 0);
    struct pollfd pfd = { .fd = p[0], .events = POLLIN };
    struct timespec ts = { 0, 300 * MS };
    long long t0 = ktest_now_ns();
    EXPECT_EQ(ppoll(&pfd, 1, &ts, NULL), 0);
    long long dt = ktest_now_ns() - t0;
    EXPECT_GE(dt, 290 * MS);
    EXPECT_LT(dt, 1000 * MS);
    waitpid(pid, NULL, 0);
}

TEST(ppoll_invalid_timeout)
{
    struct pollfd pfd = { .fd = -1 };
    struct timespec bad_ns = { 0, 1000000000 };
    struct timespec neg = { -1, 0 };
    EXPECT_ERR(ppoll(&pfd, 1, &bad_ns, NULL), EINVAL);
    EXPECT_ERR(ppoll(&pfd, 1, &neg, NULL), EINVAL);
}

/* The mask passed to ppoll unblocks SIGUSR1 only for the wait: the handler
 * runs, ppoll fails with EINTR, and the old mask is back afterwards. */
TEST(ppoll_sigmask_atomic_unblock)
{
    install(SIGUSR1, 0);
    sigset_t block, empty, now;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    sigemptyset(&empty);
    ASSERT_OK(sigprocmask(SIG_BLOCK, &block, NULL));

    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = delayed_kill(SIGUSR1, 100);
    ASSERT_GE(pid, 0);
    struct pollfd pfd = { .fd = p[0], .events = POLLIN };
    struct timespec ts = { 5, 0 };
    EXPECT_ERR(ppoll(&pfd, 1, &ts, &empty), EINTR);
    EXPECT_EQ(hits, 1);

    ASSERT_OK(sigprocmask(SIG_BLOCK, NULL, &now));
    EXPECT_TRUE(sigismember(&now, SIGUSR1));
    waitpid(pid, NULL, 0);
}

/* A signal already pending when ppoll unblocks it is delivered at once. */
TEST(ppoll_sigmask_pending_signal)
{
    install(SIGUSR1, 0);
    sigset_t block, empty;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    sigemptyset(&empty);
    ASSERT_OK(sigprocmask(SIG_BLOCK, &block, NULL));
    raise(SIGUSR1);
    EXPECT_EQ(hits, 0);

    struct pollfd pfd = { .fd = -1 };
    struct timespec ts = { 5, 0 };
    EXPECT_ERR(ppoll(&pfd, 1, &ts, &empty), EINTR);
    EXPECT_EQ(hits, 1);
}

/* The temporary mask must not stick when no signal arrives. */
TEST(ppoll_sigmask_restored_on_timeout)
{
    sigset_t block, now;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR2);
    ASSERT_OK(sigprocmask(SIG_SETMASK, &(sigset_t){0}, NULL));

    struct pollfd pfd = { .fd = -1 };
    struct timespec ts = { 0, 10 * MS };
    EXPECT_EQ(ppoll(&pfd, 1, &ts, &block), 0);

    ASSERT_OK(sigprocmask(SIG_BLOCK, NULL, &now));
    EXPECT_TRUE(!sigismember(&now, SIGUSR2));
}

/* A signal only the temporary mask unblocks is not delivered when ppoll
 * succeeds; it stays pending under the restored mask. */
TEST(ppoll_sigmask_no_delivery_on_success)
{
    install(SIGUSR1, 0);
    int p[2];
    ASSERT_OK(pipe(p));
    ASSERT_EQ(write(p[1], "x", 1), 1);
    sigset_t block, empty, now;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    sigemptyset(&empty);
    ASSERT_OK(sigprocmask(SIG_BLOCK, &block, NULL));
    raise(SIGUSR1);

    struct pollfd pfd = { .fd = p[0], .events = POLLIN };
    struct timespec ts = { 5, 0 };
    EXPECT_EQ(ppoll(&pfd, 1, &ts, &empty), 1);
    EXPECT_EQ(hits, 0);

    ASSERT_OK(sigprocmask(SIG_BLOCK, NULL, &now));
    EXPECT_TRUE(sigismember(&now, SIGUSR1));
    ASSERT_OK(sigpending(&now));
    EXPECT_TRUE(sigismember(&now, SIGUSR1));
}

/* ---------------- select ---------------- */

TEST(select_readable_pipe)
{
    int p[2];
    ASSERT_OK(pipe(p));
    fd_set r;
    struct timeval tv = { 0, 0 };
    FD_ZERO(&r);
    FD_SET(p[0], &r);
    EXPECT_EQ(select(p[0] + 1, &r, NULL, NULL, &tv), 0);
    EXPECT_TRUE(!FD_ISSET(p[0], &r));

    write(p[1], "x", 1);
    FD_ZERO(&r);
    FD_SET(p[0], &r);
    tv = (struct timeval){ 0, 0 };
    EXPECT_EQ(select(p[0] + 1, &r, NULL, NULL, &tv), 1);
    EXPECT_TRUE(FD_ISSET(p[0], &r));
}

TEST(select_writable_pipe)
{
    int p[2];
    ASSERT_OK(pipe(p));
    fd_set w;
    struct timeval tv = { 0, 0 };
    FD_ZERO(&w);
    FD_SET(p[1], &w);
    EXPECT_EQ(select(p[1] + 1, NULL, &w, NULL, &tv), 1);
    EXPECT_TRUE(FD_ISSET(p[1], &w));
}

/* Only ready fds stay set; the rest are cleared. */
TEST(select_clears_unready)
{
    int a[2], b[2];
    ASSERT_OK(pipe(a));
    ASSERT_OK(pipe(b));
    write(b[1], "x", 1);
    fd_set r;
    FD_ZERO(&r);
    FD_SET(a[0], &r);
    FD_SET(b[0], &r);
    int n = (a[0] > b[0] ? a[0] : b[0]) + 1;
    struct timeval tv = { 0, 0 };
    EXPECT_EQ(select(n, &r, NULL, NULL, &tv), 1);
    EXPECT_TRUE(!FD_ISSET(a[0], &r));
    EXPECT_TRUE(FD_ISSET(b[0], &r));
}

/* The count is set bits, so an fd ready in two sets counts twice. */
TEST(select_counts_bits_not_fds)
{
    int fd = open_tmpfile();
    ASSERT_GE(fd, 0);
    fd_set r, w;
    FD_ZERO(&r);
    FD_ZERO(&w);
    FD_SET(fd, &r);
    FD_SET(fd, &w);
    struct timeval tv = { 0, 0 };
    EXPECT_EQ(select(fd + 1, &r, &w, NULL, &tv), 2);
    EXPECT_TRUE(FD_ISSET(fd, &r));
    EXPECT_TRUE(FD_ISSET(fd, &w));
    close(fd);
}

/* fds at or above nfds are not examined, nor written back as set. */
TEST(select_respects_nfds)
{
    int p[2];
    ASSERT_OK(pipe(p));
    write(p[1], "x", 1);
    fd_set r;
    FD_ZERO(&r);
    FD_SET(p[0], &r);
    struct timeval tv = { 0, 0 };
    EXPECT_EQ(select(p[0], &r, NULL, NULL, &tv), 0);
    EXPECT_TRUE(!FD_ISSET(p[0], &r));
}

TEST(select_hup_is_readable)
{
    int p[2];
    ASSERT_OK(pipe(p));
    close(p[1]);
    fd_set r;
    FD_ZERO(&r);
    FD_SET(p[0], &r);
    struct timeval tv = { 0, 0 };
    EXPECT_EQ(select(p[0] + 1, &r, NULL, NULL, &tv), 1);
    EXPECT_TRUE(FD_ISSET(p[0], &r));
}

TEST(select_closed_fd_ebadf)
{
    int p[2];
    ASSERT_OK(pipe(p));
    close(p[0]);
    fd_set r;
    FD_ZERO(&r);
    FD_SET(p[0], &r);
    struct timeval tv = { 0, 0 };
    EXPECT_ERR(select(p[0] + 1, &r, NULL, NULL, &tv), EBADF);
}

TEST(select_invalid_args)
{
    struct timeval tv = { 0, 0 };
    EXPECT_ERR(syscall(SYS_select, -1, NULL, NULL, NULL, &tv), EINVAL);
    EXPECT_ERR(syscall(SYS_select, 1 << 20, NULL, NULL, NULL, &tv), EINVAL);
    struct timeval bad_us = { 0, 1000000 };
    EXPECT_ERR(syscall(SYS_select, 0, NULL, NULL, NULL, &bad_us), EINVAL);
    struct timeval neg = { -1, 0 };
    EXPECT_ERR(syscall(SYS_select, 0, NULL, NULL, NULL, &neg), EINVAL);
}

TEST(select_bad_pointer_efault)
{
    struct timeval tv = { 0, 0 };
    EXPECT_ERR(syscall(SYS_select, 1, (fd_set *)8, NULL, NULL, &tv), EFAULT);
}

/* select(0, NULL, NULL, NULL, &tv) is the portable sleep */
TEST(select_no_fds_sleeps)
{
    struct timeval tv = { 0, 100000 };
    long long t0 = ktest_now_ns();
    EXPECT_EQ(select(0, NULL, NULL, NULL, &tv), 0);
    long long dt = ktest_now_ns() - t0;
    EXPECT_GE(dt, 90 * MS);
    EXPECT_LT(dt, 2000 * MS);
}

TEST(select_timeout_elapses)
{
    int p[2];
    ASSERT_OK(pipe(p));
    fd_set r;
    FD_ZERO(&r);
    FD_SET(p[0], &r);
    struct timeval tv = { 0, 100000 };
    long long t0 = ktest_now_ns();
    EXPECT_EQ(select(p[0] + 1, &r, NULL, NULL, &tv), 0);
    long long dt = ktest_now_ns() - t0;
    EXPECT_GE(dt, 90 * MS);
    EXPECT_LT(dt, 2000 * MS);
    EXPECT_TRUE(!FD_ISSET(p[0], &r));
}

TEST(select_wakes_on_write)
{
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = delayed_write(p[1], 100);
    ASSERT_GE(pid, 0);
    fd_set r;
    FD_ZERO(&r);
    FD_SET(p[0], &r);
    EXPECT_EQ(select(p[0] + 1, &r, NULL, NULL, NULL), 1);
    EXPECT_TRUE(FD_ISSET(p[0], &r));
    waitpid(pid, NULL, 0);
}

TEST(select_no_stale_waiters)
{
    int p[2];
    ASSERT_OK(pipe(p));
    char c;
    for (int i = 0; i < 200; i++) {
        fd_set r;
        FD_ZERO(&r);
        FD_SET(p[0], &r);
        struct timeval tv = { 0, 1000 };
        EXPECT_EQ(select(p[0] + 1, &r, NULL, NULL, &tv), 0);
        write(p[1], "x", 1);
        read(p[0], &c, 1);
    }
}

TEST(select_interrupted_by_signal)
{
    install(SIGUSR1, SA_RESTART);
    pid_t pid = delayed_kill(SIGUSR1, 100);
    ASSERT_GE(pid, 0);
    struct timeval tv = { 5, 0 };
    EXPECT_ERR(select(0, NULL, NULL, NULL, &tv), EINTR);
    EXPECT_EQ(hits, 1);
    waitpid(pid, NULL, 0);
}

TEST(select_restart_keeps_deadline)
{
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = stop_cont_storm(100, 1500);
    ASSERT_GE(pid, 0);
    fd_set r;
    FD_ZERO(&r);
    FD_SET(p[0], &r);
    struct timeval tv = { 0, 300000 };
    long long t0 = ktest_now_ns();
    EXPECT_EQ(select(p[0] + 1, &r, NULL, NULL, &tv), 0);
    long long dt = ktest_now_ns() - t0;
    EXPECT_GE(dt, 290 * MS);
    EXPECT_LT(dt, 1000 * MS);
    waitpid(pid, NULL, 0);
}

/* ---------------- pselect ---------------- */

TEST(pselect_timeout_elapses)
{
    struct timespec ts = { 0, 100 * MS };
    long long t0 = ktest_now_ns();
    EXPECT_EQ(pselect(0, NULL, NULL, NULL, &ts, NULL), 0);
    long long dt = ktest_now_ns() - t0;
    EXPECT_GE(dt, 90 * MS);
    EXPECT_LT(dt, 2000 * MS);
}

TEST(pselect_invalid_timeout)
{
    struct timespec bad_ns = { 0, 1000000000 };
    EXPECT_ERR(pselect(0, NULL, NULL, NULL, &bad_ns, NULL), EINVAL);
}

TEST(pselect_readable_pipe)
{
    int p[2];
    ASSERT_OK(pipe(p));
    write(p[1], "x", 1);
    fd_set r;
    FD_ZERO(&r);
    FD_SET(p[0], &r);
    struct timespec ts = { 0, 0 };
    sigset_t empty;
    sigemptyset(&empty);
    EXPECT_EQ(pselect(p[0] + 1, &r, NULL, NULL, &ts, &empty), 1);
    EXPECT_TRUE(FD_ISSET(p[0], &r));
}

/* Same atomic-unblock contract as ppoll_sigmask_atomic_unblock. */
TEST(pselect_sigmask_atomic_unblock)
{
    install(SIGUSR1, 0);
    sigset_t block, empty, now;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    sigemptyset(&empty);
    ASSERT_OK(sigprocmask(SIG_BLOCK, &block, NULL));

    pid_t pid = delayed_kill(SIGUSR1, 100);
    ASSERT_GE(pid, 0);
    struct timespec ts = { 5, 0 };
    EXPECT_ERR(pselect(0, NULL, NULL, NULL, &ts, &empty), EINTR);
    EXPECT_EQ(hits, 1);

    ASSERT_OK(sigprocmask(SIG_BLOCK, NULL, &now));
    EXPECT_TRUE(sigismember(&now, SIGUSR1));
    waitpid(pid, NULL, 0);
}

TEST(pselect_sigmask_restored_on_timeout)
{
    sigset_t block, now;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR2);
    ASSERT_OK(sigprocmask(SIG_SETMASK, &(sigset_t){0}, NULL));

    struct timespec ts = { 0, 10 * MS };
    EXPECT_EQ(pselect(0, NULL, NULL, NULL, &ts, &block), 0);

    ASSERT_OK(sigprocmask(SIG_BLOCK, NULL, &now));
    EXPECT_TRUE(!sigismember(&now, SIGUSR2));
}

TEST(pselect_sigmask_no_delivery_on_success)
{
    install(SIGUSR1, 0);
    int p[2];
    ASSERT_OK(pipe(p));
    ASSERT_EQ(write(p[1], "x", 1), 1);
    sigset_t block, empty, now;
    sigemptyset(&block);
    sigaddset(&block, SIGUSR1);
    sigemptyset(&empty);
    ASSERT_OK(sigprocmask(SIG_BLOCK, &block, NULL));
    raise(SIGUSR1);

    fd_set r;
    FD_ZERO(&r);
    FD_SET(p[0], &r);
    struct timespec ts = { 5, 0 };
    EXPECT_EQ(pselect(p[0] + 1, &r, NULL, NULL, &ts, &empty), 1);
    EXPECT_EQ(hits, 0);

    ASSERT_OK(sigprocmask(SIG_BLOCK, NULL, &now));
    EXPECT_TRUE(sigismember(&now, SIGUSR1));
    ASSERT_OK(sigpending(&now));
    EXPECT_TRUE(sigismember(&now, SIGUSR1));
}

/* ---------------- tty (stdin/stdout) ---------------- */

static struct termios saved_tio;

static void tty_inject(const char *s)
{
    for (; *s; s++)
        if (ioctl(0, TIOCSTI, s) != 0) {
            FAIL("TIOCSTI failed errno=%d", errno);
            ktest_abort();
        }
}

/* Read whatever is queued without blocking. */
static void tty_drain(void)
{
    struct termios t = saved_tio;
    t.c_lflag &= ~(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &t);
    char buf[64];
    while (read(0, buf, sizeof(buf)) > 0)
        ;
}

/* stdin must be a tty we may inject into; leaves it empty, echo off,
 * canonical or not as asked. */
static void tty_setup(bool canon, int vmin)
{
    if (!isatty(0) || !isatty(1))
        SKIP("stdin/stdout not a tty");
    ASSERT_OK(tcgetattr(0, &saved_tio));
    tty_drain();
    struct termios t = saved_tio;
    t.c_lflag &= ~ECHO;
    if (canon) {
        t.c_lflag |= ICANON;
    } else {
        t.c_lflag &= ~ICANON;
        t.c_cc[VMIN] = vmin;
        t.c_cc[VTIME] = 0;
    }
    ASSERT_OK(tcsetattr(0, TCSANOW, &t));
    /* probe permission with a harmless byte, then drop it */
    if (ioctl(0, TIOCSTI, "\n") != 0) {
        tcsetattr(0, TCSANOW, &saved_tio);
        SKIP("TIOCSTI not permitted (errno=%d)", errno);
    }
    tty_drain();
    ASSERT_OK(tcsetattr(0, TCSANOW, &t));
}

static void tty_restore(void)
{
    tty_drain();
    tcsetattr(0, TCSANOW, &saved_tio);
}

static int poll1(int fd, short events, int timeout)
{
    struct pollfd pfd = { .fd = fd, .events = events };
    int r = poll(&pfd, 1, timeout);
    return r < 0 ? r : pfd.revents;
}

TEST(tty_stdout_writable)
{
    if (!isatty(1))
        SKIP("stdout not a tty");
    EXPECT_TRUE(poll1(1, POLLOUT, 0) & POLLOUT);
}

TEST(tty_canon_needs_full_line)
{
    tty_setup(true, 0);
    EXPECT_EQ(poll1(0, POLLIN, 0), 0);
    tty_inject("ab");
    EXPECT_EQ(poll1(0, POLLIN, 0), 0);
    tty_inject("\n");
    EXPECT_TRUE(poll1(0, POLLIN, 0) & POLLIN);
    char buf[8] = {0};
    EXPECT_EQ(read(0, buf, sizeof(buf)), 3);
    EXPECT_STREQ(buf, "ab\n");
    EXPECT_EQ(poll1(0, POLLIN, 0), 0);
    tty_restore();
}

TEST(tty_canon_eof_readable)
{
    tty_setup(true, 0);
    tty_inject("\x04");
    EXPECT_TRUE(poll1(0, POLLIN, 0) & POLLIN);
    char c;
    EXPECT_EQ(read(0, &c, 1), 0);
    tty_restore();
}

TEST(tty_noncanon_vmin0)
{
    tty_setup(false, 0);
    EXPECT_EQ(poll1(0, POLLIN, 0), 0);
    tty_inject("x");
    EXPECT_TRUE(poll1(0, POLLIN, 0) & POLLIN);
    tty_restore();
}

TEST(tty_noncanon_vmin3)
{
    tty_setup(false, 3);
    tty_inject("ab");
    EXPECT_EQ(poll1(0, POLLIN, 0), 0);
    tty_inject("c");
    EXPECT_TRUE(poll1(0, POLLIN, 0) & POLLIN);
    char buf[4] = {0};
    EXPECT_EQ(read(0, buf, 3), 3);
    EXPECT_STREQ(buf, "abc");
    tty_restore();
}

TEST(tty_poll_wakes_on_input)
{
    tty_setup(true, 0);
    pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        usleep(100000);
        for (const char *s = "hi\n"; *s; s++)
            ioctl(0, TIOCSTI, s);
        _exit(0);
    }
    EXPECT_TRUE(poll1(0, POLLIN, 5000) & POLLIN);
    waitpid(pid, NULL, 0);
    tty_restore();
}

TEST(tty_select_stdin_stdout)
{
    tty_setup(true, 0);
    fd_set r, w;
    FD_ZERO(&r);
    FD_ZERO(&w);
    FD_SET(0, &r);
    FD_SET(1, &w);
    struct timeval tv = { 0, 0 };
    EXPECT_EQ(select(2, &r, &w, NULL, &tv), 1);
    EXPECT_TRUE(!FD_ISSET(0, &r));
    EXPECT_TRUE(FD_ISSET(1, &w));

    tty_inject("q\n");
    FD_ZERO(&r);
    FD_SET(0, &r);
    tv = (struct timeval){ 0, 0 };
    EXPECT_EQ(select(1, &r, NULL, NULL, &tv), 1);
    EXPECT_TRUE(FD_ISSET(0, &r));
    tty_restore();
}

/* ^S stops output, ^Q restarts it. Nothing may be printed to the tty while
 * it is stopped, so results are checked only after ^Q. */
TEST(tty_flow_control_pollout)
{
    tty_setup(true, 0);
    struct termios t;
    ASSERT_OK(tcgetattr(0, &t));
    if (!(t.c_iflag & IXON))
        SKIP("IXON off");
    tty_inject("\x13");
    int stopped = poll1(1, POLLOUT, 0);
    tty_inject("\x11");
    int started = poll1(1, POLLOUT, 0);
    EXPECT_EQ(stopped & POLLOUT, 0);
    EXPECT_TRUE(started & POLLOUT);
    tty_restore();
}
