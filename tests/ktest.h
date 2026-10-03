// Minimal userspace test framework for Lilac.
//
// Each TEST() runs in its own forked child with a timeout, so a crash or hang
// in one test cannot take down the rest of the suite. Results are printed as
//   KTEST <suite>.<test> PASS|FAIL|SKIP|CRASH|TIMEOUT [detail]
// to stdout and to /dev/kmsg (which lands in qemu's debugcon log).
#ifndef KTEST_H
#define KTEST_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#define KTEST_SKIP_CODE 77
#define KTEST_DEFAULT_TIMEOUT 10

typedef void (*ktest_fn)(void *arg);

/* line orders tests: constructors run in reverse definition order */
void ktest_register(const char *name, ktest_fn fn, void *arg, int timeout, int line);

#define KTEST_REGISTER(name, secs) \
    static void ktest_body_##name(void); \
    static void ktest_wrap_##name(void *arg) { (void)arg; ktest_body_##name(); } \
    __attribute__((constructor)) static void ktest_reg_##name(void) \
    { ktest_register(#name, ktest_wrap_##name, NULL, secs, __LINE__); } \
    static void ktest_body_##name(void)

/* TEST(name) { ... } -- default timeout */
#define TEST(name) KTEST_REGISTER(name, KTEST_DEFAULT_TIMEOUT)
/* TEST_TIMEOUT(name, secs) { ... } */
#define TEST_TIMEOUT(name, secs) KTEST_REGISTER(name, secs)

/* Reporting (callable from test bodies, including forked grandchildren). */
void ktest_fail(const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
void ktest_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
_Noreturn void ktest_abort(void);
_Noreturn void ktest_skip(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#define FAIL(...) ktest_fail(__FILE__, __LINE__, __VA_ARGS__)
#define SKIP(...) ktest_skip(__VA_ARGS__)

#define EXPECT_TRUE(c) do { \
    if (!(c)) FAIL("expected true: %s", #c); \
} while (0)
#define ASSERT_TRUE(c) do { \
    if (!(c)) { FAIL("expected true: %s", #c); ktest_abort(); } \
} while (0)

#define KT_CMP_(a, op, b, fatal) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (!(_a op _b)) { \
        FAIL("expected %s %s %s (got %lld vs %lld, errno=%d %s)", \
             #a, #op, #b, _a, _b, errno, strerror(errno)); \
        if (fatal) ktest_abort(); \
    } \
} while (0)

#define EXPECT_EQ(a, b) KT_CMP_(a, ==, b, 0)
#define EXPECT_NE(a, b) KT_CMP_(a, !=, b, 0)
#define EXPECT_LT(a, b) KT_CMP_(a, <, b, 0)
#define EXPECT_LE(a, b) KT_CMP_(a, <=, b, 0)
#define EXPECT_GT(a, b) KT_CMP_(a, >, b, 0)
#define EXPECT_GE(a, b) KT_CMP_(a, >=, b, 0)
#define ASSERT_EQ(a, b) KT_CMP_(a, ==, b, 1)
#define ASSERT_NE(a, b) KT_CMP_(a, !=, b, 1)
#define ASSERT_GE(a, b) KT_CMP_(a, >=, b, 1)
#define ASSERT_GT(a, b) KT_CMP_(a, >, b, 1)

/* Syscall-style call returning >= 0 on success. Evaluates to the result. */
#define KT_OK_(expr, fatal) ({ \
    errno = 0; \
    long _r = (long)(expr); \
    if (_r < 0) { \
        FAIL("%s failed: ret=%ld errno=%d (%s)", #expr, _r, errno, strerror(errno)); \
        if (fatal) ktest_abort(); \
    } \
    _r; \
})
#define EXPECT_OK(expr) KT_OK_(expr, 0)
#define ASSERT_OK(expr) KT_OK_(expr, 1)

/* Expect -1 with errno == err. */
#define EXPECT_ERR(expr, err) do { \
    errno = 0; \
    long _r = (long)(expr); \
    int _e = errno; \
    if (_r != -1 || _e != (err)) \
        FAIL("%s: expected -1/%s, got ret=%ld errno=%d (%s)", \
             #expr, #err, _r, _e, strerror(_e)); \
} while (0)

/* pointer-returning calls (mmap etc.) */
#define ASSERT_PTR(p) do { \
    if ((p) == NULL || (void *)(p) == (void *)-1) { \
        FAIL("%s returned %p errno=%d (%s)", #p, (void *)(p), errno, strerror(errno)); \
        ktest_abort(); \
    } \
} while (0)

#define EXPECT_STREQ(a, b) do { \
    const char *_a = (a), *_b = (b); \
    if (!_a || !_b || strcmp(_a, _b) != 0) \
        FAIL("expected %s == %s (\"%s\" vs \"%s\")", #a, #b, \
             _a ? _a : "(null)", _b ? _b : "(null)"); \
} while (0)

/* ---- helpers ---- */

/* Fork, run fn(arg) in the child, _exit with its return value.
 * Returns the raw wait status (or -1). */
int ktest_run_child(int (*fn)(void *), void *arg);

/* Wait for pid with a timeout in seconds; kills it on timeout.
 * Returns wait status, or -1 on timeout/error. */
int ktest_wait_timeout(pid_t pid, int secs);

/* Create a fresh unique directory under base; returns malloc'd path. */
char *ktest_mkdtemp(const char *base);

/* Deterministic byte pattern for data-integrity checks. */
void ktest_fill(void *buf, size_t len, unsigned seed, size_t offset);
/* Returns index of first mismatch, or (size_t)-1 if all match. */
size_t ktest_check(const void *buf, size_t len, unsigned seed, size_t offset);

/* Write/read the whole buffer, retrying on short transfers. */
ssize_t ktest_write_all(int fd, const void *buf, size_t len);
ssize_t ktest_read_all(int fd, void *buf, size_t len);

/* fork+execv argv[0] with stdout (and stderr) captured into out (NUL
 * terminated, may be NULL). Returns wait status, or -1 on setup failure. */
int ktest_spawn(char *const argv[], char *out, size_t outsz);

/* Nanoseconds from CLOCK_MONOTONIC (falls back to gettimeofday). */
long long ktest_now_ns(void);

/* Free physical memory in bytes (sysinfo), or -1. */
long long ktest_free_bytes(void);

extern const char *ktest_suite;
extern const char *ktest_current;

#endif
