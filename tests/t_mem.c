// Memory management: heap, brk, mmap/munmap/mprotect/mremap, COW, faults.
#include "ktest.h"
#include <sys/mman.h>
#include <sys/syscall.h>

#define PG 4096UL

static int crash_signal(int (*fn)(void *), void *arg)
{
    int st = ktest_run_child(fn, arg);
    if (st == -1)
        return -1000; /* timeout */
    if (WIFSIGNALED(st))
        return WTERMSIG(st);
    return -WEXITSTATUS(st);
}

#define EXPECT_CRASH(fn, arg, sig) do { \
    int _s = crash_signal(fn, arg); \
    if (_s != (sig)) \
        FAIL("%s: expected death by signal %d (%s), got %s %d", #fn, sig, #sig, \
             _s == -1000 ? "timeout" : _s <= 0 ? "exit" : "signal", _s < 0 ? -_s : _s); \
} while (0)

TEST(malloc_small_many)
{
    enum { N = 2000 };
    char **ptrs = malloc(N * sizeof(char *));
    ASSERT_PTR(ptrs);
    for (int i = 0; i < N; i++) {
        size_t sz = 1 + (i * 37) % 500;
        ptrs[i] = malloc(sz);
        ASSERT_PTR(ptrs[i]);
        memset(ptrs[i], i & 0xff, sz);
    }
    for (int i = 0; i < N; i++) {
        size_t sz = 1 + (i * 37) % 500;
        for (size_t j = 0; j < sz; j++)
            if ((unsigned char)ptrs[i][j] != (i & 0xff)) {
                FAIL("block %d corrupted at %zu", i, j);
                ktest_abort();
            }
    }
    for (int i = 0; i < N; i += 2)
        free(ptrs[i]);
    for (int i = 0; i < N; i += 2) {
        ptrs[i] = malloc(64);
        ASSERT_PTR(ptrs[i]);
    }
    for (int i = 0; i < N; i++)
        free(ptrs[i]);
    free(ptrs);
}

TEST(malloc_large)
{
    size_t sz = 16 << 20;
    char *p = malloc(sz);
    ASSERT_PTR(p);
    ktest_fill(p, sz, 1, 0);
    EXPECT_EQ(ktest_check(p, sz, 1, 0), (size_t)-1);
    free(p);
    /* and again, to check the memory is reusable */
    p = malloc(sz);
    ASSERT_PTR(p);
    memset(p, 0xab, sz);
    free(p);
}

TEST(calloc_zeroed)
{
    for (int round = 0; round < 4; round++) {
        size_t sz = 256 * 1024;
        unsigned char *p = calloc(1, sz);
        ASSERT_PTR(p);
        for (size_t i = 0; i < sz; i++)
            if (p[i]) {
                FAIL("calloc byte %zu nonzero", i);
                ktest_abort();
            }
        memset(p, 0xff, sz);
        free(p);
    }
}

TEST(realloc_grow_preserves)
{
    char *p = malloc(100);
    ASSERT_PTR(p);
    ktest_fill(p, 100, 2, 0);
    for (size_t sz = 200; sz <= (4 << 20); sz *= 2) {
        p = realloc(p, sz);
        ASSERT_PTR(p);
        EXPECT_EQ(ktest_check(p, 100, 2, 0), (size_t)-1);
    }
    free(p);
}

TEST(brk_sbrk_grow_shrink)
{
    void *start = sbrk(0);
    ASSERT_TRUE(start != (void *)-1);
    void *old = sbrk(PG * 4);
    if (old == (void *)-1)
        SKIP("sbrk not supported by libc (errno=%d)", errno);
    EXPECT_EQ((uintptr_t)old, (uintptr_t)start);
    EXPECT_EQ((uintptr_t)sbrk(0), (uintptr_t)start + PG * 4);
    memset(old, 0x5a, PG * 4);
    EXPECT_EQ(((unsigned char *)old)[PG * 4 - 1], 0x5a);
    EXPECT_TRUE(sbrk(-(intptr_t)(PG * 4)) != (void *)-1);
    EXPECT_EQ((uintptr_t)sbrk(0), (uintptr_t)start);
}

TEST(brk_raw_syscall)
{
    /* musl's malloc doesn't use brk, so exercise it directly */
    long cur = syscall(SYS_brk, 0);
    ASSERT_GT(cur, 0);
    long want = cur + 8 * PG;
    long r = syscall(SYS_brk, want);
    EXPECT_EQ(r, want);
    if (r == want) {
        memset((void *)cur, 0x11, 8 * PG);
        EXPECT_EQ(((volatile char *)cur)[8 * PG - 1], 0x11);
        EXPECT_EQ(syscall(SYS_brk, cur), cur);
    }
}

TEST(mmap_anon_zero_filled)
{
    size_t len = 64 * PG;
    unsigned char *p = mmap(NULL, len, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_PTR(p);
    EXPECT_EQ((uintptr_t)p % PG, 0);
    for (size_t i = 0; i < len; i++)
        if (p[i]) {
            FAIL("byte %zu nonzero", i);
            break;
        }
    ktest_fill(p, len, 3, 0);
    EXPECT_EQ(ktest_check(p, len, 3, 0), (size_t)-1);
    EXPECT_OK(munmap(p, len));
}

TEST(mmap_reuse_zero_filled)
{
    /* pages returned to the kernel must be zeroed when handed out again */
    size_t len = 256 * PG;
    for (int round = 0; round < 3; round++) {
        unsigned char *p = mmap(NULL, len, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ASSERT_PTR(p);
        for (size_t i = 0; i < len; i += 64)
            if (p[i]) {
                FAIL("round %d: stale data at %zu", round, i);
                ktest_abort();
            }
        memset(p, 0xee, len);
        munmap(p, len);
    }
}

static int touch_after_munmap(void *arg)
{
    volatile char *p = mmap(NULL, PG, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return 2;
    p[0] = 1;
    munmap((void *)p, PG);
    p[0] = 2;
    return 0;
}

TEST(munmap_then_access_segv)
{
    EXPECT_CRASH(touch_after_munmap, NULL, SIGSEGV);
}

static int write_readonly(void *arg)
{
    volatile char *p = mmap(NULL, PG, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return 2;
    p[0] = 1;
    if (mprotect((void *)p, PG, PROT_READ) != 0)
        return 3;
    if (p[0] != 1)
        return 4;
    p[0] = 2;
    return 0;
}

TEST(mprotect_readonly_write_segv)
{
    EXPECT_CRASH(write_readonly, NULL, SIGSEGV);
}

static int write_prot_read_map(void *arg)
{
    volatile char *p = mmap(NULL, PG, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return 2;
    if (p[0] != 0)
        return 3;
    p[0] = 1;
    return 0;
}

TEST(mmap_prot_read_write_segv)
{
    EXPECT_CRASH(write_prot_read_map, NULL, SIGSEGV);
}

static int read_prot_none(void *arg)
{
    volatile char *p = mmap(NULL, PG, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return 2;
    return p[0];
}

TEST(mmap_prot_none_read_segv)
{
    EXPECT_CRASH(read_prot_none, NULL, SIGSEGV);
}

TEST(mprotect_restore_write)
{
    char *p = mmap(NULL, 2 * PG, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_PTR(p);
    p[0] = 'a';
    EXPECT_OK(mprotect(p, 2 * PG, PROT_READ));
    EXPECT_OK(mprotect(p, 2 * PG, PROT_READ | PROT_WRITE));
    p[0] = 'b';
    p[PG] = 'c';
    EXPECT_EQ(p[0], 'b');
    EXPECT_EQ(p[PG], 'c');
    munmap(p, 2 * PG);
}

static int exec_from_noexec(void *arg)
{
    unsigned char *p = mmap(NULL, PG, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return 2;
    p[0] = 0xc3; /* ret */
    ((void (*)(void))p)();
    return 0;
}

TEST(exec_noexec_mapping_segv)
{
    EXPECT_CRASH(exec_from_noexec, NULL, SIGSEGV);
}

TEST(mprotect_exec_runs)
{
    unsigned char *p = mmap(NULL, PG, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_PTR(p);
    /* mov eax, 42; ret */
    unsigned char code[] = { 0xb8, 42, 0, 0, 0, 0xc3 };
    memcpy(p, code, sizeof(code));
    ASSERT_OK(mprotect(p, PG, PROT_READ | PROT_EXEC));
    int r = ((int (*)(void))p)();
    EXPECT_EQ(r, 42);
    munmap(p, PG);
}

TEST(mmap_fixed)
{
    char *p = mmap(NULL, 4 * PG, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_PTR(p);
    memset(p, 'x', 4 * PG);
    char *q = mmap(p + PG, PG, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    EXPECT_EQ((uintptr_t)q, (uintptr_t)(p + PG));
    if (q == p + PG) {
        EXPECT_EQ(q[0], 0);          /* fresh mapping replaces old one */
        EXPECT_EQ(p[0], 'x');
        EXPECT_EQ(p[2 * PG], 'x');
    }
    munmap(p, 4 * PG);
}

TEST(mmap_hint_not_fixed)
{
    /* a hint on an occupied address must not clobber the existing mapping */
    char *p = mmap(NULL, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_PTR(p);
    p[0] = 'k';
    char *q = mmap(p, PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_PTR(q);
    EXPECT_NE((uintptr_t)q, (uintptr_t)p);
    EXPECT_EQ(p[0], 'k');
    munmap(p, PG);
    munmap(q, PG);
}

TEST(munmap_middle_split)
{
    char *p = mmap(NULL, 3 * PG, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_PTR(p);
    p[0] = 'a';
    p[PG] = 'b';
    p[2 * PG] = 'c';
    EXPECT_OK(munmap(p + PG, PG));
    EXPECT_EQ(p[0], 'a');
    EXPECT_EQ(p[2 * PG], 'c');
    p[0] = 'd';
    p[2 * PG] = 'e';
    /* the hole can be mapped again */
    char *q = mmap(p + PG, PG, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    EXPECT_EQ((uintptr_t)q, (uintptr_t)(p + PG));
    munmap(p, 3 * PG);
}

static int touch_hole(void *arg)
{
    char *p = mmap(NULL, 3 * PG, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        return 2;
    munmap(p + PG, PG);
    ((volatile char *)p)[PG] = 1;
    return 0;
}

TEST(munmap_middle_hole_segv)
{
    EXPECT_CRASH(touch_hole, NULL, SIGSEGV);
}

TEST(mmap_invalid_args)
{
    EXPECT_TRUE(mmap(NULL, 0, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == MAP_FAILED);
    EXPECT_TRUE(mmap(NULL, PG, PROT_READ, MAP_ANONYMOUS, -1, 0) == MAP_FAILED);
    EXPECT_TRUE(mmap((void *)(PG + 1), PG, PROT_READ,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED);
    EXPECT_TRUE(mmap(NULL, PG, PROT_READ, MAP_PRIVATE, 12345, 0) == MAP_FAILED);
}

TEST(mmap_shared_anon_across_fork)
{
    volatile int *shared = mmap(NULL, PG, PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ASSERT_PTR(shared);
    shared[0] = 1;
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        shared[0] = 2;
        shared[1] = 1234;
        _exit(0);
    }
    waitpid(pid, NULL, 0);
    EXPECT_EQ(shared[0], 2);
    EXPECT_EQ(shared[1], 1234);
    munmap((void *)shared, PG);
}

TEST(cow_private_across_fork)
{
    char *p = mmap(NULL, 2 * PG, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_PTR(p);
    strcpy(p, "parent0");
    strcpy(p + PG, "parent1");
    int to_child[2], to_parent[2];
    ASSERT_OK(pipe(to_child));
    ASSERT_OK(pipe(to_parent));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        char t;
        read(to_child[0], &t, 1);
        if (strcmp(p, "parent0") != 0)
            _exit(2); /* parent's post-fork write leaked */
        strcpy(p + PG, "child1");
        write(to_parent[1], "x", 1);
        read(to_child[0], &t, 1);
        if (strcmp(p + PG, "child1") != 0)
            _exit(3);
        _exit(0);
    }
    strcpy(p, "after-fork");
    write(to_child[1], "x", 1);
    char t;
    read(to_parent[0], &t, 1);
    EXPECT_STREQ(p + PG, "parent1");
    write(to_child[1], "x", 1);
    int st;
    waitpid(pid, &st, 0);
    EXPECT_TRUE(WIFEXITED(st));
    EXPECT_EQ(WEXITSTATUS(st), 0);
    munmap(p, 2 * PG);
}

TEST(cow_heap_and_stack)
{
    volatile int stackvar = 10;
    int *heap = malloc(sizeof(int));
    ASSERT_PTR(heap);
    *heap = 20;
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        stackvar = 11;
        *heap = 21;
        _exit(stackvar == 11 && *heap == 21 ? 0 : 1);
    }
    int st;
    waitpid(pid, &st, 0);
    EXPECT_EQ(WEXITSTATUS(st), 0);
    EXPECT_EQ(stackvar, 10);
    EXPECT_EQ(*heap, 20);
    free(heap);
}

TEST(cow_after_child_exit)
{
    /* after the child exits, the parent must still be able to write its pages */
    size_t len = 32 * PG;
    char *p = malloc(len);
    ASSERT_PTR(p);
    memset(p, 1, len);
    for (int i = 0; i < 5; i++) {
        pid_t pid = ASSERT_OK(fork());
        if (pid == 0)
            _exit(p[len - 1] == 1 + i ? 0 : 1);
        waitpid(pid, NULL, 0);
        memset(p, 2 + i, len);
    }
    EXPECT_EQ(p[0], 6);
    free(p);
}

TEST(mremap_grow_preserves)
{
    size_t old = 4 * PG, new = 64 * PG;
    char *p = mmap(NULL, old, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_PTR(p);
    ktest_fill(p, old, 4, 0);
    char *q = mremap(p, old, new, MREMAP_MAYMOVE);
    ASSERT_PTR(q);
    EXPECT_EQ(ktest_check(q, old, 4, 0), (size_t)-1);
    memset(q + old, 0x33, new - old);
    EXPECT_EQ(q[new - 1], 0x33);
    munmap(q, new);
}

TEST(mremap_shrink)
{
    char *p = mmap(NULL, 8 * PG, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_PTR(p);
    p[0] = 'z';
    char *q = mremap(p, 8 * PG, 2 * PG, 0);
    EXPECT_EQ((uintptr_t)q, (uintptr_t)p);
    EXPECT_EQ(q[0], 'z');
    munmap(q, 2 * PG);
}

TEST(memfd_write_read_mmap)
{
    int fd = memfd_create("ktest", 0);
    ASSERT_GE(fd, 0);
    const char msg[] = "memfd contents";
    EXPECT_EQ(write(fd, msg, sizeof(msg)), sizeof(msg));
    EXPECT_EQ(lseek(fd, 0, SEEK_SET), 0);
    char buf[32] = {0};
    EXPECT_EQ(read(fd, buf, sizeof(msg)), sizeof(msg));
    EXPECT_STREQ(buf, msg);
    char *m = mmap(NULL, PG, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_PTR(m);
    EXPECT_STREQ(m, msg);
    strcpy(m, "changed");
    EXPECT_EQ(pread(fd, buf, 8, 0), 8);
    EXPECT_STREQ(buf, "changed");
    munmap(m, PG);
    close(fd);
}

TEST(memfd_ftruncate_mmap)
{
    int fd = memfd_create("ktest2", 0);
    ASSERT_GE(fd, 0);
    EXPECT_OK(ftruncate(fd, 4 * PG));
    struct stat st;
    EXPECT_OK(fstat(fd, &st));
    EXPECT_EQ(st.st_size, 4 * PG);
    char *m = mmap(NULL, 4 * PG, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_PTR(m);
    m[3 * PG] = 'q';
    char c = 0;
    EXPECT_EQ(pread(fd, &c, 1, 3 * PG), 1);
    EXPECT_EQ(c, 'q');
    munmap(m, 4 * PG);
    close(fd);
}

TEST(memfd_shared_across_fork)
{
    int fd = memfd_create("ktest3", 0);
    ASSERT_GE(fd, 0);
    char zero[PG] = {0};
    write(fd, zero, PG);
    volatile char *m = mmap(NULL, PG, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ASSERT_PTR(m);
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        m[10] = 'c';
        _exit(0);
    }
    waitpid(pid, NULL, 0);
    EXPECT_EQ(m[10], 'c');
    munmap((void *)m, PG);
    close(fd);
}

TEST_TIMEOUT(large_anon_touch_64m, 30)
{
    size_t len = 64UL << 20;
    unsigned char *p = mmap(NULL, len, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_PTR(p);
    for (size_t i = 0; i < len; i += PG)
        p[i] = (unsigned char)(i / PG);
    for (size_t i = 0; i < len; i += PG)
        if (p[i] != (unsigned char)(i / PG)) {
            FAIL("page %zu mismatch", i / PG);
            break;
        }
    EXPECT_OK(munmap(p, len));
}

TEST_TIMEOUT(map_unmap_loop_no_leak, 30)
{
    /* 1000 x 1MB map/touch/unmap; would exhaust 256MB of RAM if pages leak */
    size_t len = 1 << 20;
    for (int i = 0; i < 1000; i++) {
        char *p = mmap(NULL, len, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p == MAP_FAILED) {
            FAIL("mmap failed at iteration %d errno=%d", i, errno);
            ktest_abort();
        }
        for (size_t j = 0; j < len; j += PG)
            p[j] = 1;
        int r = munmap(p, len);
        if (r != 0) {
            FAIL("munmap(%p, %zu) failed at iteration %d: ret=%d errno=%d (%s)",
                 (void *)p, len, i, r, errno, strerror(errno));
            ktest_abort();
        }
    }
}

TEST_TIMEOUT(fork_exit_loop_no_leak, 60)
{
    /* each child touches 4MB; leaking it would exhaust memory */
    size_t len = 4 << 20;
    for (int i = 0; i < 100; i++) {
        pid_t pid = fork();
        ASSERT_GE(pid, 0);
        if (pid == 0) {
            char *p = malloc(len);
            if (!p)
                _exit(2);
            memset(p, 1, len);
            _exit(0);
        }
        int st;
        waitpid(pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
            FAIL("iteration %d: child status 0x%x", i, st);
            ktest_abort();
        }
    }
}

static int recurse(int depth)
{
    volatile char buf[256];
    buf[0] = (char)depth;
    if (depth == 0)
        return buf[0];
    return recurse(depth - 1) + buf[0];
}

TEST(stack_growth_1mb)
{
    /* ~4000 frames of >256 bytes: grows the stack by >1MB */
    int expect = 0;
    for (int d = 0; d <= 4000; d++)
        expect += (char)d;
    EXPECT_EQ(recurse(4000), expect);
}

static int big_stack_array(void *arg)
{
    volatile char buf[2 << 20];
    buf[0] = 1;
    buf[sizeof(buf) - 1] = 2;
    return buf[0] + buf[sizeof(buf) - 1] == 3 ? 0 : 1;
}

TEST(stack_growth_2mb_array)
{
    int st = ktest_run_child(big_stack_array, NULL);
    EXPECT_TRUE(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0);
    if (st != -1 && WIFSIGNALED(st))
        FAIL("child killed by signal %d", WTERMSIG(st));
}

static int null_deref(void *arg)
{
    return *(volatile int *)0;
}

TEST(null_deref_segv)
{
    EXPECT_CRASH(null_deref, NULL, SIGSEGV);
}

static int kernel_addr_read(void *arg)
{
    return *(volatile int *)0xffffffff80000000UL;
}

TEST(kernel_address_read_segv)
{
    EXPECT_CRASH(kernel_addr_read, NULL, SIGSEGV);
}

static int physmap_write(void *arg)
{
    *(volatile int *)0xffff900000000000UL = 1;
    return 0;
}

TEST(physmap_write_segv)
{
    EXPECT_CRASH(physmap_write, NULL, SIGSEGV);
}

static int noncanonical(void *arg)
{
    return *(volatile int *)0x0000800000000000UL;
}

TEST(noncanonical_address_segv)
{
    /* non-canonical addresses raise #GP, which should also be SIGSEGV */
    EXPECT_CRASH(noncanonical, NULL, SIGSEGV);
}

static int write_text(void *arg)
{
    *(volatile unsigned char *)(uintptr_t)&write_text = 0x90;
    return 0;
}

TEST(write_to_text_segv)
{
    EXPECT_CRASH(write_text, NULL, SIGSEGV);
}

static const char rodata_str[] = "read only";
static int write_rodata(void *arg)
{
    *(volatile char *)rodata_str = 'R';
    return 0;
}

TEST(write_to_rodata_segv)
{
    EXPECT_CRASH(write_rodata, NULL, SIGSEGV);
}

TEST(msync_ok)
{
    char *p = mmap(NULL, PG, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    ASSERT_PTR(p);
    p[0] = 1;
    EXPECT_OK(msync(p, PG, MS_SYNC));
    munmap(p, PG);
}
