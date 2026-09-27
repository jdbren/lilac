// Probes that may crash or wedge the kernel. Run last so a panic here does not
// hide the results of every other suite.
#include "ktest.h"
#include <sys/mman.h>
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
