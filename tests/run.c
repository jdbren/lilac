// Test runner. Runs every suite in /tests in order and reports results.
//
// When started as pid 1 (the test disk image installs it as /sbin/init) it
// sets up stdio on /dev/tty0 and powers the machine off when finished, so
// scripts/run-tests.sh can run the whole suite unattended.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#define SUITE_TIMEOUT 180

static const char *suites[] = {
    "proc",
    "mem",
    "signal",
    "fs",
    "pipe",
    "thread",
    "time",
    "misc",
    "zz_hazard", /* last: may take the kernel down */
};

static int kmsg_fd = -1;

static void say(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof(buf))
        n = sizeof(buf) - 1;
    ssize_t r = write(STDOUT_FILENO, buf, n);
    if (kmsg_fd >= 0)
        r = write(kmsg_fd, buf, n);
    (void)r;
}

static volatile sig_atomic_t timed_out;
static void on_alarm(int sig)
{
    (void)sig;
    timed_out = 1;
}

static void reap_orphans(void)
{
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

static int run_suite(const char *name, char **filters)
{
    char path[128];
    snprintf(path, sizeof(path), "/tests/t_%s", name);
    if (access(path, X_OK) != 0) {
        say("KTEST SUITE %s MISSING (%s)\n", name, strerror(errno));
        return 1;
    }

    say("KTEST SUITE %s START\n", name);
    pid_t pid = fork();
    if (pid < 0) {
        say("KTEST SUITE %s FORKFAIL %s\n", name, strerror(errno));
        return 1;
    }
    if (pid == 0) {
        int n = 0;
        while (filters && filters[n])
            n++;
        char **argv = calloc(n + 2, sizeof(char *));
        argv[0] = path;
        for (int i = 0; i < n; i++)
            argv[i + 1] = filters[i];
        execv(path, argv);
        _exit(127);
    }

    struct sigaction sa = {0};
    sa.sa_handler = on_alarm;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, NULL);
    timed_out = 0;
    alarm(SUITE_TIMEOUT);

    int status = 0;
    pid_t r;
    do {
        r = waitpid(pid, &status, 0);
    } while (r < 0 && errno == EINTR && !timed_out);
    alarm(0);

    if (r != pid) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        say("KTEST SUITE %s TIMEOUT after %ds\n", name, SUITE_TIMEOUT);
        reap_orphans();
        return 1;
    }
    reap_orphans();

    if (WIFEXITED(status)) {
        say("KTEST SUITE %s END exit=%d\n", name, WEXITSTATUS(status));
        return WEXITSTATUS(status) != 0;
    }
    say("KTEST SUITE %s END signal=%d\n", name, WTERMSIG(status));
    return 1;
}

int main(int argc, char **argv)
{
    bool is_init = getpid() == 1;

    if (is_init) {
        setsid();
        int fd = open("/dev/tty0", O_RDWR);
        if (fd >= 0) {
            if (fd != 0) dup2(fd, 0);
            dup2(0, 1);
            dup2(0, 2);
        }
        setenv("PATH", "/bin:/sbin:/usr/bin", 1);
        setenv("HOME", "/root", 1);
        chdir("/");
    }
    kmsg_fd = open("/dev/kmsg", O_WRONLY);
    if (kmsg_fd < 0 && is_init)
        say("warning: /dev/kmsg unavailable, results only on screen\n");

    say("KTEST BEGIN\n");
    int failed_suites = 0;

    /* /tests/plan (written by scripts/run-tests.sh): one "suite [args...]"
       per line. Otherwise: run [suite [args...]], or every suite. */
    FILE *plan = argc == 1 ? fopen("/tests/plan", "r") : NULL;
    if (plan) {
        char line[512];
        while (fgets(line, sizeof(line), plan)) {
            char *args[32];
            int n = 0;
            for (char *tok = strtok(line, " \t\n"); tok && n < 31; tok = strtok(NULL, " \t\n"))
                args[n++] = tok;
            args[n] = NULL;
            if (n > 0)
                failed_suites += run_suite(args[0], &args[1]);
        }
        fclose(plan);
    } else {
        const char *only = argc > 1 ? argv[1] : NULL;
        char **filters = argc > 2 ? &argv[2] : NULL;
        for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
            if (only && strcmp(only, suites[i]) != 0)
                continue;
            failed_suites += run_suite(suites[i], filters);
        }
    }
    say("KTEST DONE failed_suites=%d\n", failed_suites);

    if (is_init) {
        sync();
        syscall(SYS_reboot, 1);
        say("KTEST poweroff failed: %s\n", strerror(errno));
        for (;;)
            pause();
    }
    return failed_suites != 0;
}
