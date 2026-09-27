#include "ktest.h"
#include <fnmatch.h>
#include <sys/time.h>
#include <time.h>

#define MAX_TESTS 512

struct ktest {
    const char *name;
    ktest_fn fn;
    void *arg;
    int timeout;
    int line;
    int seq;
};

static struct ktest tests[MAX_TESTS];
static int ntests;

const char *ktest_suite = "?";
const char *ktest_current = "?";
static int kmsg_fd = -1;
static bool failed;
static bool in_test;

void ktest_register(const char *name, ktest_fn fn, void *arg, int timeout, int line)
{
    if (ntests >= MAX_TESTS) {
        fprintf(stderr, "ktest: too many tests\n");
        _exit(2);
    }
    tests[ntests] = (struct ktest){ name, fn, arg, timeout, line, ntests };
    ntests++;
}

static void emit(const char *buf, size_t len)
{
    ssize_t r = write(STDOUT_FILENO, buf, len);
    (void)r;
    if (kmsg_fd >= 0) {
        r = write(kmsg_fd, buf, len);
        (void)r;
    }
}

static void emitf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof(buf))
        n = sizeof(buf) - 1;
    emit(buf, n);
}

static void vdetail(const char *prefix, const char *fmt, va_list ap)
{
    char msg[400];
    vsnprintf(msg, sizeof(msg), fmt, ap);
    emitf("KTEST   %s.%s: %s%s\n", ktest_suite, ktest_current, prefix, msg);
}

void ktest_fail(const char *file, int line, const char *fmt, ...)
{
    char where[128];
    const char *base = strrchr(file, '/');
    snprintf(where, sizeof(where), "%s:%d: ", base ? base + 1 : file, line);
    va_list ap;
    va_start(ap, fmt);
    vdetail(where, fmt, ap);
    va_end(ap);
    failed = true;
}

void ktest_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vdetail("note: ", fmt, ap);
    va_end(ap);
}

_Noreturn void ktest_abort(void)
{
    _exit(1);
}

_Noreturn void ktest_skip(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vdetail("skip: ", fmt, ap);
    va_end(ap);
    _exit(KTEST_SKIP_CODE);
}

/* ---------------- helpers ---------------- */

int ktest_run_child(int (*fn)(void *), void *arg)
{
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
        int rc = fn(arg);
        _exit(failed && rc == 0 ? 1 : rc);
    }
    return ktest_wait_timeout(pid, KTEST_DEFAULT_TIMEOUT);
}

static volatile sig_atomic_t wait_alarm;
static void wait_alarm_handler(int sig)
{
    (void)sig;
    wait_alarm = 1;
}

int ktest_wait_timeout(pid_t pid, int secs)
{
    struct sigaction sa = {0}, old;
    sa.sa_handler = wait_alarm_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* no SA_RESTART: we want waitpid to return EINTR */
    sigaction(SIGALRM, &sa, &old);
    wait_alarm = 0;
    unsigned prev = alarm(secs);

    int status;
    pid_t r;
    for (;;) {
        r = waitpid(pid, &status, 0);
        if (r == pid)
            break;
        if (r < 0 && errno == EINTR && !wait_alarm)
            continue;
        break;
    }

    alarm(prev);
    sigaction(SIGALRM, &old, NULL);

    if (r == pid)
        return status;
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return -1;
}

char *ktest_mkdtemp(const char *base)
{
    static int counter;
    char *path = malloc(256);
    if (!path)
        return NULL;
    for (int i = 0; i < 100; i++) {
        snprintf(path, 256, "%s/k%d_%d", base, (int)getpid(), counter++);
        if (mkdir(path, 0755) == 0)
            return path;
        if (errno != EEXIST)
            break;
    }
    free(path);
    return NULL;
}

static inline uint8_t pattern_byte(unsigned seed, size_t pos)
{
    uint32_t x = (uint32_t)(pos * 2654435761u) ^ (seed * 0x9e3779b9u);
    x ^= x >> 13;
    return (uint8_t)(x ^ (pos >> 8));
}

void ktest_fill(void *buf, size_t len, unsigned seed, size_t offset)
{
    uint8_t *p = buf;
    for (size_t i = 0; i < len; i++)
        p[i] = pattern_byte(seed, offset + i);
}

size_t ktest_check(const void *buf, size_t len, unsigned seed, size_t offset)
{
    const uint8_t *p = buf;
    for (size_t i = 0; i < len; i++)
        if (p[i] != pattern_byte(seed, offset + i))
            return i;
    return (size_t)-1;
}

ssize_t ktest_write_all(int fd, const void *buf, size_t len)
{
    const char *p = buf;
    size_t done = 0;
    while (done < len) {
        ssize_t r = write(fd, p + done, len - done);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return done ? (ssize_t)done : r;
        done += r;
    }
    return done;
}

ssize_t ktest_read_all(int fd, void *buf, size_t len)
{
    char *p = buf;
    size_t done = 0;
    while (done < len) {
        ssize_t r = read(fd, p + done, len - done);
        if (r < 0 && errno == EINTR)
            continue;
        if (r < 0)
            return done ? (ssize_t)done : r;
        if (r == 0)
            break;
        done += r;
    }
    return done;
}

int ktest_spawn(char *const argv[], char *out, size_t outsz)
{
    int p[2];
    if (pipe(p) < 0)
        return -1;
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        return -1;
    }
    if (pid == 0) {
        close(p[0]);
        dup2(p[1], 1);
        dup2(p[1], 2);
        if (p[1] > 2)
            close(p[1]);
        execv(argv[0], argv);
        _exit(127);
    }
    close(p[1]);
    size_t len = 0;
    char sink[256];
    for (;;) {
        char *dst = out && len + 1 < outsz ? out + len : sink;
        size_t room = out && len + 1 < outsz ? outsz - 1 - len : sizeof(sink);
        ssize_t r = read(p[0], dst, room);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            break;
        if (dst != sink)
            len += r;
    }
    if (out && outsz)
        out[len] = '\0';
    close(p[0]);
    return ktest_wait_timeout(pid, KTEST_DEFAULT_TIMEOUT);
}

long long ktest_now_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return ts.tv_sec * 1000000000LL + ts.tv_nsec;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000000LL + tv.tv_usec * 1000LL;
}

/* ---------------- runner ---------------- */

static const char *signame(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS:  return "SIGBUS";
    case SIGFPE:  return "SIGFPE";
    case SIGILL:  return "SIGILL";
    case SIGABRT: return "SIGABRT";
    case SIGKILL: return "SIGKILL";
    case SIGALRM: return "SIGALRM";
    case SIGPIPE: return "SIGPIPE";
    case SIGTERM: return "SIGTERM";
    case SIGTRAP: return "SIGTRAP";
    default: return "signal";
    }
}

static bool selected(const char *name, int nglobs, char **globs)
{
    if (nglobs == 0)
        return true;
    for (int i = 0; i < nglobs; i++)
        if (fnmatch(globs[i], name, 0) == 0)
            return true;
    return false;
}

static int run_one(struct ktest *t)
{
    ktest_current = t->name;
    fflush(NULL);
    /* lets the host attribute a panic or hang to this test */
    if (kmsg_fd >= 0) {
        char buf[160];
        int n = snprintf(buf, sizeof(buf), "KTEST %s.%s START\n", ktest_suite, t->name);
        ssize_t r = write(kmsg_fd, buf, n);
        (void)r;
    }

    long long start = ktest_now_ns();
    pid_t pid = fork();
    if (pid < 0) {
        emitf("KTEST %s.%s FAIL fork: %s\n", ktest_suite, t->name, strerror(errno));
        return 1;
    }
    if (pid == 0) {
        in_test = true;
        failed = false;
        signal(SIGALRM, SIG_DFL);
        alarm(t->timeout);
        t->fn(t->arg);
        fflush(NULL);
        _exit(failed ? 1 : 0);
    }

    int status = ktest_wait_timeout(pid, t->timeout + 3);
    long long ms = (ktest_now_ns() - start) / 1000000;

    if (status == -1 || (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM)) {
        emitf("KTEST %s.%s TIMEOUT (%ds)\n", ktest_suite, t->name, t->timeout);
        return 1;
    }
    if (WIFSIGNALED(status)) {
        emitf("KTEST %s.%s CRASH %s (%d)\n", ktest_suite, t->name,
              signame(WTERMSIG(status)), WTERMSIG(status));
        return 1;
    }
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 0) {
            emitf("KTEST %s.%s PASS (%lldms)\n", ktest_suite, t->name, ms);
            return 0;
        }
        if (code == KTEST_SKIP_CODE) {
            emitf("KTEST %s.%s SKIP\n", ktest_suite, t->name);
            return 0;
        }
        emitf("KTEST %s.%s FAIL\n", ktest_suite, t->name);
        return 1;
    }
    emitf("KTEST %s.%s FAIL unexpected wait status 0x%x\n", ktest_suite, t->name, status);
    return 1;
}

static int cmp_tests(const void *a, const void *b)
{
    const struct ktest *x = a, *y = b;
    if (x->line != y->line)
        return x->line - y->line;
    return x->seq - y->seq;
}

int main(int argc, char **argv)
{
    qsort(tests, ntests, sizeof(tests[0]), cmp_tests);

    const char *base = strrchr(argv[0], '/');
    base = base ? base + 1 : argv[0];
    if (strncmp(base, "t_", 2) == 0)
        base += 2;
    ktest_suite = base;

    kmsg_fd = open("/dev/kmsg", O_WRONLY);
    setvbuf(stdout, NULL, _IONBF, 0);

    /* usage: t_<suite> [-l] [-s resume-after-test] [glob...] */
    const char *resume_after = NULL;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        if (!strcmp(argv[argi], "-l")) {
            for (int i = 0; i < ntests; i++)
                printf("%s\n", tests[i].name);
            return 0;
        }
        if (!strcmp(argv[argi], "-s") && argi + 1 < argc) {
            resume_after = argv[argi + 1];
            argi += 2;
            continue;
        }
        fprintf(stderr, "usage: %s [-l] [-s test] [glob...]\n", argv[0]);
        return 2;
    }

    int start = 0;
    if (resume_after) {
        for (int i = 0; i < ntests; i++)
            if (!strcmp(tests[i].name, resume_after))
                start = i + 1;
    }

    int nfail = 0, nrun = 0;
    for (int i = start; i < ntests; i++) {
        if (!selected(tests[i].name, argc - argi, argv + argi))
            continue;
        nrun++;
        nfail += run_one(&tests[i]);
    }
    emitf("KTEST %s summary: %d run, %d failed\n", ktest_suite, nrun, nfail);
    return nfail > 125 ? 125 : nfail;
}
