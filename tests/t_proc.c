// Process lifecycle: fork, exit, wait, exec, process groups/sessions.
#include "ktest.h"

#define HELPER "/tests/helper"

TEST(fork_returns_child_pid)
{
    pid_t parent = getpid();
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        pid_t me = getpid();
        pid_t pp = getppid();
        write(p[1], &me, sizeof(me));
        write(p[1], &pp, sizeof(pp));
        _exit(0);
    }
    pid_t child_self, child_parent;
    EXPECT_EQ(read(p[0], &child_self, sizeof(child_self)), sizeof(child_self));
    EXPECT_EQ(read(p[0], &child_parent, sizeof(child_parent)), sizeof(child_parent));
    EXPECT_GT(pid, 0);
    EXPECT_EQ(child_self, pid);
    EXPECT_EQ(child_parent, parent);
    EXPECT_NE(pid, parent);
    int st;
    EXPECT_EQ(waitpid(pid, &st, 0), pid);
}

TEST(exit_status_propagates)
{
    int codes[] = { 0, 1, 42, 255 };
    for (unsigned i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        pid_t pid = ASSERT_OK(fork());
        if (pid == 0)
            _exit(codes[i]);
        int st = 0;
        EXPECT_EQ(waitpid(pid, &st, 0), pid);
        EXPECT_TRUE(WIFEXITED(st));
        EXPECT_EQ(WEXITSTATUS(st), codes[i]);
    }
}

TEST(exit_from_main_return)
{
    char *argv[] = { HELPER, "exit", "7", NULL };
    int st = ktest_spawn(argv, NULL, 0);
    ASSERT_TRUE(st != -1);
    EXPECT_TRUE(WIFEXITED(st));
    EXPECT_EQ(WEXITSTATUS(st), 7);
}

TEST(wait_any_multiple_children)
{
    enum { N = 8 };
    pid_t pids[N];
    for (int i = 0; i < N; i++) {
        pids[i] = ASSERT_OK(fork());
        if (pids[i] == 0) {
            usleep(1000 * (N - i));
            _exit(10 + i);
        }
    }
    int seen = 0;
    for (int n = 0; n < N; n++) {
        int st;
        pid_t r = wait(&st);
        ASSERT_GT(r, 0);
        int idx = -1;
        for (int i = 0; i < N; i++)
            if (pids[i] == r)
                idx = i;
        ASSERT_GE(idx, 0);
        EXPECT_TRUE(WIFEXITED(st));
        EXPECT_EQ(WEXITSTATUS(st), 10 + idx);
        EXPECT_TRUE(!(seen & (1 << idx)));
        seen |= 1 << idx;
    }
    EXPECT_EQ(seen, (1 << N) - 1);
    EXPECT_ERR(wait(NULL), ECHILD);
}

TEST(waitpid_specific_child_order)
{
    pid_t a = ASSERT_OK(fork());
    if (a == 0)
        _exit(1);
    pid_t b = ASSERT_OK(fork());
    if (b == 0) {
        usleep(20000);
        _exit(2);
    }
    int st;
    /* wait for the slower one first */
    EXPECT_EQ(waitpid(b, &st, 0), b);
    EXPECT_EQ(WEXITSTATUS(st), 2);
    EXPECT_EQ(waitpid(a, &st, 0), a);
    EXPECT_EQ(WEXITSTATUS(st), 1);
}

TEST(waitpid_wnohang)
{
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        char c;
        close(p[1]);
        read(p[0], &c, 1);
        _exit(3);
    }
    close(p[0]);
    int st;
    EXPECT_EQ(waitpid(pid, &st, WNOHANG), 0);
    write(p[1], "x", 1);
    close(p[1]);
    EXPECT_EQ(waitpid(pid, &st, 0), pid);
    EXPECT_EQ(WEXITSTATUS(st), 3);
}

TEST(waitpid_no_children_echild)
{
    EXPECT_ERR(waitpid(-1, NULL, 0), ECHILD);
    EXPECT_ERR(waitpid(-1, NULL, WNOHANG), ECHILD);
}

TEST(waitpid_not_my_child_echild)
{
    /* pid 1 is never our child */
    EXPECT_ERR(waitpid(1, NULL, 0), ECHILD);
}

TEST(zombie_reaped_once)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0)
        _exit(0);
    usleep(20000); /* let it become a zombie */
    EXPECT_EQ(waitpid(pid, NULL, 0), pid);
    EXPECT_ERR(waitpid(pid, NULL, 0), ECHILD);
}

TEST(waitpid_pgrp_zero)
{
    /* waitpid(0) waits for any child in our process group */
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0)
        _exit(5);
    int st = 0;
    pid_t r = waitpid(0, &st, 0);
    if (r < 0)
        waitpid(pid, NULL, 0);
    EXPECT_EQ(r, pid);
    EXPECT_EQ(WEXITSTATUS(st), 5);
}

TEST(fork_memory_isolated)
{
    volatile int x = 1;
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        x = 2;
        _exit(x == 2 ? 0 : 1);
    }
    int st;
    waitpid(pid, &st, 0);
    EXPECT_EQ(WEXITSTATUS(st), 0);
    EXPECT_EQ(x, 1);
}

TEST(fork_inherits_fds)
{
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        write(p[1], "hi", 2);
        _exit(0);
    }
    close(p[1]);
    char buf[4] = {0};
    EXPECT_EQ(read(p[0], buf, sizeof(buf)), 2);
    EXPECT_STREQ(buf, "hi");
    waitpid(pid, NULL, 0);
}

TEST(fork_many_sequential)
{
    for (int i = 0; i < 100; i++) {
        pid_t pid = fork();
        ASSERT_GE(pid, 0);
        if (pid == 0)
            _exit(i & 0x7f);
        int st;
        ASSERT_EQ(waitpid(pid, &st, 0), pid);
        ASSERT_EQ(WEXITSTATUS(st), i & 0x7f);
    }
}

TEST(fork_many_concurrent)
{
    enum { N = 32 };
    pid_t pids[N];
    for (int i = 0; i < N; i++) {
        pids[i] = fork();
        ASSERT_GE(pids[i], 0);
        if (pids[i] == 0) {
            usleep(10000);
            _exit(0);
        }
    }
    for (int i = 0; i < N; i++) {
        int st;
        EXPECT_EQ(waitpid(pids[i], &st, 0), pids[i]);
        EXPECT_EQ(WEXITSTATUS(st), 0);
    }
}

TEST(nested_fork)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        pid_t g = fork();
        if (g < 0)
            _exit(10);
        if (g == 0)
            _exit(4);
        int st;
        if (waitpid(g, &st, 0) != g)
            _exit(11);
        _exit(WEXITSTATUS(st) + 1);
    }
    int st;
    EXPECT_EQ(waitpid(pid, &st, 0), pid);
    EXPECT_EQ(WEXITSTATUS(st), 5);
}

TEST(execve_argv)
{
    char out[128];
    char *argv[] = { HELPER, "argv", "one", "two", "three", NULL };
    int st = ktest_spawn(argv, out, sizeof(out));
    EXPECT_TRUE(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0);
    EXPECT_STREQ(out, "one two three\n");
}

TEST(execve_argc)
{
    char *argv[] = { HELPER, "argc", "a", "b", "c", "d", NULL };
    int st = ktest_spawn(argv, NULL, 0);
    EXPECT_TRUE(WIFEXITED(st));
    EXPECT_EQ(WEXITSTATUS(st), 6);
}

TEST(execve_envp)
{
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        dup2(p[1], 1);
        char *argv[] = { HELPER, "env", "KTEST_VAR", NULL };
        char *envp[] = { "FOO=bar", "KTEST_VAR=hello world", NULL };
        execve(HELPER, argv, envp);
        _exit(127);
    }
    close(p[1]);
    char buf[64] = {0};
    ktest_read_all(p[0], buf, sizeof(buf) - 1);
    int st;
    waitpid(pid, &st, 0);
    EXPECT_EQ(WEXITSTATUS(st), 0);
    EXPECT_STREQ(buf, "hello world\n");
}

TEST(execve_inherits_environ)
{
    setenv("KTEST_INHERIT", "yes", 1);
    char out[64];
    char *argv[] = { HELPER, "env", "KTEST_INHERIT", NULL };
    ktest_spawn(argv, out, sizeof(out));
    EXPECT_STREQ(out, "yes\n");
}

TEST(execve_enoent)
{
    char *argv[] = { "/nonexistent/prog", NULL };
    char *envp[] = { NULL };
    EXPECT_ERR(execve("/nonexistent/prog", argv, envp), ENOENT);
}

TEST(execve_not_executable)
{
    /* a directory cannot be executed */
    char *argv[] = { "/tests", NULL };
    char *envp[] = { NULL };
    errno = 0;
    int r = execve("/tests", argv, envp);
    EXPECT_EQ(r, -1);
    EXPECT_TRUE(errno == EACCES || errno == EISDIR || errno == ENOEXEC);
}

TEST(execve_garbage_file_enoexec)
{
    const char *path = "/tmp/ktest_garbage_exec";
    int fd = ASSERT_OK(open(path, O_CREAT | O_WRONLY, 0755));
    write(fd, "this is not an elf file\n", 24);
    close(fd);
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        char *argv[] = { (char *)path, NULL };
        char *envp[] = { NULL };
        execve(path, argv, envp);
        _exit(errno == ENOEXEC ? 0 : errno == 0 ? 200 : errno);
    }
    int st = ktest_wait_timeout(pid, 5);
    EXPECT_TRUE(st != -1 && WIFEXITED(st));
    if (st != -1 && WIFEXITED(st) && WEXITSTATUS(st) != 0)
        FAIL("execve of non-ELF file: expected ENOEXEC, got errno %d (%s)",
             WEXITSTATUS(st), strerror(WEXITSTATUS(st)));
    if (st != -1 && WIFSIGNALED(st))
        FAIL("execve of non-ELF file killed the process with signal %d", WTERMSIG(st));
    unlink(path);
}

TEST(exec_preserves_pid)
{
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        dup2(p[1], 1);
        char *argv[] = { HELPER, "pid", NULL };
        execv(HELPER, argv);
        _exit(127);
    }
    close(p[1]);
    char buf[64] = {0};
    ktest_read_all(p[0], buf, sizeof(buf) - 1);
    waitpid(pid, NULL, 0);
    int cpid = 0, cppid = 0;
    EXPECT_EQ(sscanf(buf, "%d %d", &cpid, &cppid), 2);
    EXPECT_EQ(cpid, pid);
    EXPECT_EQ(cppid, getpid());
}

TEST(exec_bin_echo)
{
    char out[64];
    char *argv[] = { "/bin/echo", "hello", NULL };
    int st = ktest_spawn(argv, out, sizeof(out));
    EXPECT_TRUE(WIFEXITED(st) && WEXITSTATUS(st) == 0);
    EXPECT_STREQ(out, "hello\n");
}

TEST(vfork_exec)
{
    pid_t pid = vfork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        char *argv[] = { HELPER, "exit", "9", NULL };
        execv(HELPER, argv);
        _exit(127);
    }
    int st;
    EXPECT_EQ(waitpid(pid, &st, 0), pid);
    EXPECT_EQ(WEXITSTATUS(st), 9);
}

TEST(vfork_parent_suspended)
{
    volatile int order = 0;
    pid_t pid = vfork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        order = 1; /* shared memory with parent under vfork */
        _exit(0);
    }
    EXPECT_EQ(order, 1);
    waitpid(pid, NULL, 0);
}

TEST(kill_child_sigkill)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        for (;;)
            pause();
    }
    usleep(10000);
    EXPECT_OK(kill(pid, SIGKILL));
    int st;
    EXPECT_EQ(waitpid(pid, &st, 0), pid);
    EXPECT_TRUE(WIFSIGNALED(st));
    EXPECT_EQ(WTERMSIG(st), SIGKILL);
}

TEST(kill_busy_child)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        for (volatile unsigned long i = 0;; i++)
            ;
    }
    usleep(10000);
    EXPECT_OK(kill(pid, SIGKILL));
    int st = ktest_wait_timeout(pid, 3);
    EXPECT_NE(st, -1);
    EXPECT_TRUE(WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);
}

TEST(getpgid_setpgid)
{
    EXPECT_EQ(getpgid(0), getpgrp());
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        if (setpgid(0, 0) != 0)
            _exit(1);
        if (getpgid(0) != getpid())
            _exit(2);
        _exit(0);
    }
    int st;
    waitpid(pid, &st, 0);
    EXPECT_EQ(WEXITSTATUS(st), 0);
}

TEST(setpgid_child_from_parent)
{
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        char c;
        close(p[1]);
        read(p[0], &c, 1);
        _exit(getpgid(0) == getpid() ? 0 : 1);
    }
    close(p[0]);
    EXPECT_OK(setpgid(pid, pid));
    EXPECT_EQ(getpgid(pid), pid);
    write(p[1], "x", 1);
    int st;
    waitpid(pid, &st, 0);
    EXPECT_EQ(WEXITSTATUS(st), 0);
}

TEST(setsid_new_session)
{
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        pid_t s = setsid();
        if (s != getpid())
            _exit(1);
        if (getsid(0) != getpid())
            _exit(2);
        if (getpgid(0) != getpid())
            _exit(3);
        /* a session leader cannot create another session */
        if (setsid() != -1 || errno != EPERM)
            _exit(4);
        _exit(0);
    }
    int st;
    waitpid(pid, &st, 0);
    EXPECT_EQ(WEXITSTATUS(st), 0);
}

TEST(kill_process_group)
{
    pid_t leader = ASSERT_OK(fork());
    if (leader == 0) {
        setpgid(0, 0);
        for (;;)
            pause();
    }
    setpgid(leader, leader);
    pid_t member = ASSERT_OK(fork());
    if (member == 0) {
        setpgid(0, leader);
        for (;;)
            pause();
    }
    setpgid(member, leader);
    usleep(20000);
    EXPECT_OK(kill(-leader, SIGTERM));
    int st;
    st = ktest_wait_timeout(leader, 3);
    EXPECT_TRUE(st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);
    st = ktest_wait_timeout(member, 3);
    EXPECT_TRUE(st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM);
}

TEST(orphan_reparented)
{
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        pid_t g = fork();
        if (g == 0) {
            /* wait for our parent to exit */
            for (int i = 0; i < 200 && getppid() == pid; i++)
                usleep(5000);
            pid_t pp = getppid();
            write(p[1], &pp, sizeof(pp));
            _exit(0);
        }
        _exit(0);
    }
    close(p[1]);
    waitpid(pid, NULL, 0);
    pid_t newparent = 0;
    EXPECT_EQ(ktest_read_all(p[0], &newparent, sizeof(newparent)), sizeof(newparent));
    EXPECT_NE(newparent, pid);
    /* Linux reparents to init (1) or a subreaper; Lilac has no subreapers */
    EXPECT_EQ(newparent, 1);
}

TEST(getpid_stable)
{
    pid_t a = getpid();
    EXPECT_GT(a, 0);
    EXPECT_EQ(getpid(), a);
    EXPECT_GT(getppid(), 0);
}

TEST(exit_closes_pipe_write_end)
{
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        close(p[0]);
        /* exit without closing p[1] explicitly */
        _exit(0);
    }
    close(p[1]);
    char c;
    EXPECT_EQ(read(p[0], &c, 1), 0);
    waitpid(pid, NULL, 0);
}
