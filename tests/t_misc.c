// Miscellaneous syscalls: identity, uname, errno conventions, EFAULT, tty.
#include "ktest.h"
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <termios.h>

TEST(uid_gid_root)
{
    EXPECT_EQ(getuid(), 0);
    EXPECT_EQ(geteuid(), 0);
    EXPECT_EQ(getgid(), 0);
    EXPECT_EQ(getegid(), 0);
}

TEST(uname_works)
{
    struct utsname u;
    ASSERT_OK(uname(&u));
    EXPECT_TRUE(u.sysname[0] != 0);
    EXPECT_TRUE(u.machine[0] != 0);
    ktest_log("uname: %s %s %s %s", u.sysname, u.release, u.version, u.machine);
}

TEST(unknown_syscall_enosys)
{
    EXPECT_ERR(syscall(500), ENOSYS);
    EXPECT_ERR(syscall(100000), ENOSYS);
}

TEST(write_efault)
{
    int p[2];
    ASSERT_OK(pipe(p));
    EXPECT_ERR(write(p[1], (void *)1, 10), EFAULT);
    EXPECT_ERR(write(p[1], (void *)0xffffffff80000000UL, 10), EFAULT);
}

TEST(read_efault)
{
    int p[2];
    ASSERT_OK(pipe(p));
    write(p[1], "abcdefgh", 8);
    EXPECT_ERR(read(p[0], (void *)1, 8), EFAULT);
    write(p[1], "abcdefgh", 8);
    /* reading into kernel memory must not be allowed */
    EXPECT_ERR(read(p[0], (void *)0xffffffff80000000UL, 8), EFAULT);
}

TEST(open_path_efault)
{
    EXPECT_ERR(open(NULL, O_RDONLY), EFAULT);
    EXPECT_ERR(open((char *)0xffffffff80000000UL, O_RDONLY), EFAULT);
}

TEST(stat_efault)
{
    /* raw syscalls: musl's stat() may copy through a local buffer */
    EXPECT_ERR(syscall(SYS_stat, "/", (void *)8), EFAULT);
    EXPECT_ERR(syscall(SYS_stat, "/", (void *)0xffff900000000000UL), EFAULT);
    EXPECT_ERR(syscall(SYS_fstat, 0, (void *)0xffffffff80000000UL), EFAULT);
}

TEST(pipe_efault)
{
    EXPECT_ERR(syscall(SYS_pipe, (int *)0xffffffff80000000UL), EFAULT);
}

TEST(clock_efault)
{
    EXPECT_ERR(syscall(SYS_gettimeofday, (void *)0xffffffff80000000UL, NULL), EFAULT);
}

TEST(writev_efault)
{
    int p[2];
    ASSERT_OK(pipe(p));
    struct iovec iov = { (void *)0xffffffff80000000UL, 8 };
    EXPECT_ERR(writev(p[1], &iov, 1), EFAULT);
    EXPECT_ERR(writev(p[1], (struct iovec *)8, 1), EFAULT);
}

TEST(execve_efault)
{
    EXPECT_ERR(execve((char *)0xffffffff80000000UL, NULL, NULL), EFAULT);
}

TEST(bad_fd_ebadf)
{
    char c;
    EXPECT_ERR(read(1000, &c, 1), EBADF);
    EXPECT_ERR(write(-5, &c, 1), EBADF);
    struct stat st;
    EXPECT_ERR(fstat(1000, &st), EBADF);
    EXPECT_ERR(dup(1000), EBADF);
    EXPECT_ERR(lseek(1000, 0, SEEK_SET), EBADF);
    EXPECT_ERR(fcntl(1000, F_GETFD), EBADF);
}

TEST(zero_length_io)
{
    int p[2];
    ASSERT_OK(pipe(p));
    EXPECT_EQ(write(p[1], "", 0), 0);
    /* zero-length read must not block even on an empty pipe */
    char c;
    EXPECT_EQ(read(p[0], &c, 0), 0);
}

TEST(environ_present)
{
    extern char **environ;
    EXPECT_TRUE(environ != NULL);
    EXPECT_OK(setenv("KTEST_X", "1", 1));
    EXPECT_STREQ(getenv("KTEST_X"), "1");
}

TEST(tty_isatty_and_termios)
{
    int fd = open("/dev/tty0", O_RDWR | O_NOCTTY);
    if (fd < 0)
        SKIP("no /dev/tty0 (errno=%d)", errno);
    EXPECT_TRUE(isatty(fd));
    struct termios t;
    EXPECT_OK(tcgetattr(fd, &t));
    struct winsize ws;
    if (ioctl(fd, TIOCGWINSZ, &ws) == 0) {
        EXPECT_GT(ws.ws_row, 0);
        EXPECT_GT(ws.ws_col, 0);
    } else {
        FAIL("TIOCGWINSZ errno=%d", errno);
    }
    close(fd);
    int p[2];
    ASSERT_OK(pipe(p));
    EXPECT_TRUE(!isatty(p[0]));
}

TEST(tty_write)
{
    int fd = open("/dev/tty0", O_WRONLY | O_NOCTTY);
    if (fd < 0)
        SKIP("no /dev/tty0");
    const char msg[] = "ktest: tty write\n";
    EXPECT_EQ(write(fd, msg, sizeof(msg) - 1), sizeof(msg) - 1);
    close(fd);
}

TEST(sysconf_values)
{
    EXPECT_EQ(sysconf(_SC_PAGESIZE), 4096);
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    ktest_log("online cpus: %ld", n);
    EXPECT_GE(n, 1);
}

TEST(arch_prctl_fs)
{
    /* musl sets FS for TLS at startup; reading it back must match */
    unsigned long fs = 0;
    EXPECT_OK(syscall(SYS_arch_prctl, 0x1003 /* ARCH_GET_FS */, &fs));
    EXPECT_NE(fs, 0);
    unsigned long self;
    __asm__("mov %%fs:0, %0" : "=r"(self));
    EXPECT_EQ(self, fs);
}

TEST(set_tid_address_returns_tid)
{
    static int word;
    EXPECT_EQ(syscall(SYS_set_tid_address, &word), gettid());
}

TEST(getrlimit_works)
{
    struct rlimit { unsigned long cur, max; } rl;
    EXPECT_OK(syscall(SYS_getrlimit, 7 /* RLIMIT_NOFILE */, &rl));
}

TEST(stdio_buffered_output)
{
    /* printf through a pipe, flushed at exit */
    int p[2];
    ASSERT_OK(pipe(p));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        dup2(p[1], 1);
        setvbuf(stdout, NULL, _IOFBF, 0);
        printf("buffered %d\n", 123);
        exit(0);
    }
    close(p[1]);
    char buf[32] = {0};
    ktest_read_all(p[0], buf, sizeof(buf) - 1);
    waitpid(pid, NULL, 0);
    EXPECT_STREQ(buf, "buffered 123\n");
}

TEST(dynamic_loader_present)
{
    struct stat st;
    EXPECT_OK(stat("/lib/ld-lilac.so.1", &st));
}
