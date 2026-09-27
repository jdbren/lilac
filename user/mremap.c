#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/wait.h>

#define PG 4096

static int failures;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (errno %d)\n", msg, errno); failures++; } \
    else printf("ok: %s\n", msg); \
} while (0)

static void fill(char *p, size_t len, char seed)
{
    for (size_t i = 0; i < len; i++)
        p[i] = (char)(seed + i / PG);
}

static int verify(const char *p, size_t len, char seed)
{
    for (size_t i = 0; i < len; i++)
        if (p[i] != (char)(seed + i / PG))
            return 0;
    return 1;
}

// Returns 1 if touching addr in a child process segfaults
static int faults(void *addr)
{
    pid_t pid = fork();
    if (pid == 0) {
        *(volatile char *)addr = 1;
        _exit(0);
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV;
}

int main(void)
{
    char *p = mmap(NULL, 2 * PG, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    fill(p, 2 * PG, 'a');

    // Grow in place (nothing mapped after it yet)
    char *q = mremap(p, 2 * PG, 4 * PG, 0);
    CHECK(q == p, "grow in place without MAYMOVE");
    CHECK(verify(q, 2 * PG, 'a'), "data preserved after in-place grow");
    CHECK(q[3 * PG] == 0, "grown tail is zero-filled");
    fill(q, 4 * PG, 'a');

    // Shrink
    q = mremap(p, 4 * PG, 3 * PG, 0);
    CHECK(q == p, "shrink");
    CHECK(faults(p + 3 * PG), "shrunk tail is unmapped");

    // Block the tail, then growth must move
    char *block = mmap(p + 3 * PG, PG, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    CHECK(block == p + 3 * PG, "map blocker after region");
    errno = 0;
    q = mremap(p, 3 * PG, 6 * PG, 0);
    CHECK(q == MAP_FAILED && errno == ENOMEM, "grow without MAYMOVE fails when blocked");

    q = mremap(p, 3 * PG, 6 * PG, MREMAP_MAYMOVE);
    CHECK(q != MAP_FAILED && q != p, "grow with MAYMOVE moves");
    CHECK(verify(q, 3 * PG, 'a'), "data preserved after move");
    CHECK(faults(p), "old address unmapped after move");
    q[5 * PG] = 'z';
    CHECK(q[5 * PG] == 'z', "moved tail is writable");

    // COW: fork shares the pages, then the parent moves them
    fill(q, 3 * PG, 'k');
    pid_t pid = fork();
    if (pid == 0) {
        sleep(1);
        _exit(verify(q, 3 * PG, 'k') ? 0 : 1);
    }
    char *target = mmap(NULL, 6 * PG, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    char *r = mremap(q, 6 * PG, 6 * PG, MREMAP_MAYMOVE | MREMAP_FIXED, target);
    CHECK(r == target, "MREMAP_FIXED move");
    CHECK(verify(r, 3 * PG, 'k'), "data preserved after fixed move");
    fill(r, 3 * PG, 'q'); // COW break in the parent after the move
    int status;
    waitpid(pid, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
        "child's COW copy unaffected by parent's move + write");
    CHECK(verify(r, 3 * PG, 'q'), "parent sees its own writes");

    // PROT_NONE page sharing a page table with a region that gets moved away
    char *pn = mmap(NULL, 4 * PG, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    fill(pn, 4 * PG, 'p');
    CHECK(mprotect(pn, PG, PROT_NONE) == 0, "mprotect PROT_NONE");
    char *dst = mmap(NULL, 3 * PG, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    char *moved = mremap(pn + PG, 3 * PG, 3 * PG, MREMAP_MAYMOVE | MREMAP_FIXED, dst);
    CHECK(moved == dst, "move neighbour of PROT_NONE page");
    CHECK(faults(pn), "PROT_NONE page still faults");
    CHECK(mprotect(pn, PG, PROT_READ | PROT_WRITE) == 0, "mprotect back to RW");
    CHECK(verify(pn, PG, 'p'), "PROT_NONE page keeps data after neighbour move");
    CHECK(moved[0] == 'p' + 1 && moved[2 * PG] == 'p' + 3, "moved neighbour keeps data");
    munmap(pn, PG);
    munmap(moved, 3 * PG);

    // mprotect RO -> RW on a private page shared with a child must not let
    // the parent write through to the child's copy
    char *cw = mmap(NULL, PG, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    fill(cw, PG, 'c');
    CHECK(mprotect(cw, PG, PROT_READ) == 0, "mprotect RO before fork");
    int pfd[2];
    pipe(pfd);
    pid = fork();
    if (pid == 0) {
        char c;
        close(pfd[1]);
        read(pfd[0], &c, 1); // wait for the parent's write
        _exit(verify(cw, PG, 'c') ? 0 : 1);
    }
    close(pfd[0]);
    CHECK(mprotect(cw, PG, PROT_READ | PROT_WRITE) == 0, "mprotect RW after fork");
    fill(cw, PG, 'w');
    write(pfd[1], "x", 1);
    close(pfd[1]);
    waitpid(pid, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
        "child's copy unaffected by parent's write after mprotect RW");
    CHECK(verify(cw, PG, 'w'), "parent sees its own write after mprotect RW");
    munmap(cw, PG);

    // Error cases
    errno = 0;
    CHECK(mremap(r + 1, PG, 2 * PG, MREMAP_MAYMOVE) == MAP_FAILED && errno == EINVAL,
        "unaligned old_addr -> EINVAL");
    errno = 0;
    CHECK(mremap(r, PG, 2 * PG, MREMAP_FIXED, target) == MAP_FAILED && errno == EINVAL,
        "FIXED without MAYMOVE -> EINVAL");
    errno = 0;
    CHECK(mremap((void *)0x1000, PG, 2 * PG, MREMAP_MAYMOVE) == MAP_FAILED && errno == EFAULT,
        "unmapped old range -> EFAULT");

    // musl realloc of large blocks goes through mremap
    size_t big = 1 << 20;
    char *m = malloc(big);
    memset(m, 0x5a, big);
    m = realloc(m, 4 * big);
    int good = m != NULL;
    for (size_t i = 0; good && i < big; i++)
        good = m[i] == 0x5a;
    memset(m + big, 0x11, 3 * big);
    CHECK(good, "large realloc preserves data");
    free(m);

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures != 0;
}
