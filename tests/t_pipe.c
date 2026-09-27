// Pipes: data transfer, EOF, EPIPE/SIGPIPE, blocking, nonblocking, redirection.
#include "ktest.h"
#include <poll.h>

TEST(basic_transfer)
{
    int p[2];
    ASSERT_OK(pipe(p));
    EXPECT_EQ(write(p[1], "abc", 3), 3);
    char buf[8] = {0};
    EXPECT_EQ(read(p[0], buf, sizeof(buf)), 3);
    EXPECT_STREQ(buf, "abc");
    close(p[0]);
    close(p[1]);
}

TEST(pipe_fds_distinct_and_typed)
{
    int p[2];
    ASSERT_OK(pipe(p));
    EXPECT_NE(p[0], p[1]);
    struct stat st;
    EXPECT_OK(fstat(p[0], &st));
    EXPECT_TRUE(S_ISFIFO(st.st_mode));
    EXPECT_ERR(lseek(p[0], 0, SEEK_SET), ESPIPE);
    EXPECT_ERR(write(p[0], "x", 1), EBADF);
    char c;
    EXPECT_ERR(read(p[1], &c, 1), EBADF);
}

TEST(partial_reads_preserve_order)
{
    int p[2];
    ASSERT_OK(pipe(p));
    write(p[1], "0123456789", 10);
    char buf[4] = {0};
    EXPECT_EQ(read(p[0], buf, 3), 3);
    EXPECT_EQ(memcmp(buf, "012", 3), 0);
    EXPECT_EQ(read(p[0], buf, 3), 3);
    EXPECT_EQ(memcmp(buf, "345", 3), 0);
    char rest[8] = {0};
    EXPECT_EQ(read(p[0], rest, sizeof(rest)), 4);
    EXPECT_STREQ(rest, "6789");
}

TEST(eof_when_writers_closed)
{
    int p[2];
    ASSERT_OK(pipe(p));
    write(p[1], "z", 1);
    close(p[1]);
    char c;
    EXPECT_EQ(read(p[0], &c, 1), 1);
    EXPECT_EQ(read(p[0], &c, 1), 0);
    EXPECT_EQ(read(p[0], &c, 1), 0);
}

TEST(eof_needs_all_writers_closed)
{
    int p[2];
    ASSERT_OK(pipe(p));
    int w2 = dup(p[1]);
    close(p[1]);
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        usleep(100000);
        write(w2, "late", 4);
        _exit(0);
    }
    close(w2);
    char buf[8] = {0};
    EXPECT_EQ(ktest_read_all(p[0], buf, sizeof(buf)), 4);
    EXPECT_STREQ(buf, "late");
    waitpid(pid, NULL, 0);
}

TEST(epipe_and_sigpipe)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        int p[2];
        pipe(p);
        close(p[0]);
        write(p[1], "x", 1);
        _exit(0);
    }
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGPIPE);

    signal(SIGPIPE, SIG_IGN);
    int p[2];
    ASSERT_OK(pipe(p));
    close(p[0]);
    EXPECT_ERR(write(p[1], "x", 1), EPIPE);
}

TEST(blocked_reader_wakes)
{
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        usleep(200000);
        write(p[1], "wake", 4);
        _exit(0);
    }
    long long t0 = ktest_now_ns();
    char buf[8] = {0};
    EXPECT_EQ(read(p[0], buf, 4), 4);
    EXPECT_GE(ktest_now_ns() - t0, 100000000LL);
    EXPECT_STREQ(buf, "wake");
    waitpid(pid, NULL, 0);
}

TEST(blocked_writer_wakes)
{
    /* fill the pipe, then check the writer blocks until the reader drains */
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        close(p[0]);
        static char big[256 * 1024];
        ssize_t n = ktest_write_all(p[1], big, sizeof(big));
        _exit(n == (ssize_t)sizeof(big) ? 0 : 1);
    }
    close(p[1]);
    usleep(100000);
    static char buf[4096];
    size_t total = 0;
    ssize_t r;
    while ((r = read(p[0], buf, sizeof(buf))) > 0)
        total += r;
    EXPECT_EQ(total, 256 * 1024);
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

TEST_TIMEOUT(transfer_1mb_checksum, 30)
{
    enum { SIZE = 1 << 20 };
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        close(p[0]);
        char *buf = malloc(SIZE);
        ktest_fill(buf, SIZE, 11, 0);
        /* odd chunk sizes */
        size_t off = 0, chunk = 1;
        while (off < SIZE) {
            size_t n = SIZE - off < chunk ? SIZE - off : chunk;
            if (ktest_write_all(p[1], buf + off, n) != (ssize_t)n)
                _exit(1);
            off += n;
            chunk = chunk * 3 % 7919 + 1;
        }
        _exit(0);
    }
    close(p[1]);
    char *buf = malloc(SIZE);
    ASSERT_PTR(buf);
    EXPECT_EQ(ktest_read_all(p[0], buf, SIZE), SIZE);
    size_t bad = ktest_check(buf, SIZE, 11, 0);
    if (bad != (size_t)-1)
        FAIL("mismatch at byte %zu", bad);
    char c;
    EXPECT_EQ(read(p[0], &c, 1), 0);
    int st = ktest_wait_timeout(pid, 10);
    EXPECT_TRUE(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

TEST(atomic_small_writes)
{
    /* writes <= PIPE_BUF from multiple writers must not interleave */
    enum { WRITERS = 4, MSGS = 200, LEN = 64 };
    int p[2];
    ASSERT_OK(pipe(p));
    for (int w = 0; w < WRITERS; w++) {
        pid_t pid = ASSERT_OK(fork());
        if (pid == 0) {
            close(p[0]);
            char msg[LEN];
            memset(msg, 'a' + w, LEN);
            for (int i = 0; i < MSGS; i++)
                if (write(p[1], msg, LEN) != LEN)
                    _exit(1);
            _exit(0);
        }
    }
    close(p[1]);
    char msg[LEN];
    int counts[WRITERS] = {0};
    for (int i = 0; i < WRITERS * MSGS; i++) {
        if (ktest_read_all(p[0], msg, LEN) != LEN) {
            FAIL("short read at message %d", i);
            break;
        }
        int w = msg[0] - 'a';
        for (int j = 1; j < LEN; j++)
            if (msg[j] != msg[0]) {
                FAIL("interleaved write in message %d", i);
                ktest_abort();
            }
        if (w >= 0 && w < WRITERS)
            counts[w]++;
    }
    for (int w = 0; w < WRITERS; w++)
        EXPECT_EQ(counts[w], MSGS);
    while (wait(NULL) > 0)
        ;
}

TEST(nonblocking_eagain)
{
    int p[2];
    ASSERT_OK(pipe(p));
    int fl = fcntl(p[0], F_GETFL);
    ASSERT_OK(fcntl(p[0], F_SETFL, fl | O_NONBLOCK));
    char c;
    EXPECT_ERR(read(p[0], &c, 1), EAGAIN);
    write(p[1], "q", 1);
    EXPECT_EQ(read(p[0], &c, 1), 1);

    /* non-blocking writer eventually gets EAGAIN on a full pipe */
    fl = fcntl(p[1], F_GETFL);
    ASSERT_OK(fcntl(p[1], F_SETFL, fl | O_NONBLOCK));
    char buf[4096] = {0};
    ssize_t r;
    size_t total = 0;
    for (int i = 0; i < 1024; i++) {
        r = write(p[1], buf, sizeof(buf));
        if (r < 0)
            break;
        total += r;
    }
    EXPECT_EQ(r, -1);
    EXPECT_EQ(errno, EAGAIN);
    EXPECT_GT(total, 0);
}

TEST(pipe2_flags)
{
    int p[2];
    ASSERT_OK(pipe2(p, O_CLOEXEC | O_NONBLOCK));
    EXPECT_TRUE(fcntl(p[0], F_GETFD) & FD_CLOEXEC);
    EXPECT_TRUE(fcntl(p[1], F_GETFL) & O_NONBLOCK);
}

TEST(redirect_stdout_of_exec)
{
    char out[64];
    char *argv[] = { "/bin/echo", "piped-output", NULL };
    int st = ktest_spawn(argv, out, sizeof(out));
    EXPECT_TRUE(st != -1 && WIFEXITED(st));
    EXPECT_STREQ(out, "piped-output\n");
}

TEST(pipeline_two_processes)
{
    /* helper argv | helper cat, like "echo | cat" in a shell */
    int p[2], out[2];
    ASSERT_OK(pipe(p));
    ASSERT_OK(pipe(out));
    pid_t a = ASSERT_OK(fork());
    if (a == 0) {
        dup2(p[1], 1);
        close(p[0]);
        close(p[1]);
        close(out[0]);
        close(out[1]);
        char *argv[] = { "/tests/helper", "argv", "through", "pipeline", NULL };
        execv(argv[0], argv);
        _exit(127);
    }
    pid_t b = ASSERT_OK(fork());
    if (b == 0) {
        dup2(p[0], 0);
        dup2(out[1], 1);
        close(p[0]);
        close(p[1]);
        close(out[0]);
        close(out[1]);
        char *argv[] = { "/tests/helper", "cat", NULL };
        execv(argv[0], argv);
        _exit(127);
    }
    close(p[0]);
    close(p[1]);
    close(out[1]);
    char buf[64] = {0};
    ktest_read_all(out[0], buf, sizeof(buf) - 1);
    EXPECT_STREQ(buf, "through pipeline\n");
    int st;
    EXPECT_TRUE((st = ktest_wait_timeout(a, 5)) != -1 && WEXITSTATUS(st) == 0);
    EXPECT_TRUE((st = ktest_wait_timeout(b, 5)) != -1 && WEXITSTATUS(st) == 0);
}

TEST(poll_readable)
{
    int p[2];
    ASSERT_OK(pipe(p));
    struct pollfd pfd = { .fd = p[0], .events = POLLIN };
    EXPECT_EQ(poll(&pfd, 1, 0), 0);
    write(p[1], "x", 1);
    EXPECT_EQ(poll(&pfd, 1, 100), 1);
    EXPECT_TRUE(pfd.revents & POLLIN);
}

TEST(many_pipes)
{
    int fds[40][2];
    int n = 0;
    for (; n < 40; n++)
        if (pipe(fds[n]) != 0) {
            FAIL("pipe #%d failed errno=%d", n, errno);
            break;
        }
    for (int i = 0; i < n; i++) {
        char c = 'a' + i % 26, d = 0;
        write(fds[i][1], &c, 1);
        read(fds[i][0], &d, 1);
        if (c != d)
            FAIL("pipe %d crossed data", i);
        close(fds[i][0]);
        close(fds[i][1]);
    }
}
