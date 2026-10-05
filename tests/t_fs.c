// Filesystem / VFS tests. Most cases are registered once per writable
// filesystem (tmpfs at /tmp, fat32 at /ktest on the root fs) and run in a
// fresh scratch directory. ext2 (/mnt) is read-only and is checked against a
// manifest generated on the host.
#include "ktest.h"
#include <dirent.h>
#include <strings.h>
#include <sys/uio.h>
#include <time.h>

struct fsx {
    const char *fs;
    const char *base;
};

static const struct fsx fses[] = {
    { "tmpfs", "/tmp" },
    { "fat32", "/ktest" },
};

typedef void (*fs_body)(const char *dir, const struct fsx *fs);

struct fs_case {
    fs_body body;
    const struct fsx *fs;
};

static void fs_run(void *arg)
{
    struct fs_case *c = arg;
    struct stat st;
    if (stat(c->fs->base, &st) != 0) {
        if (mkdir(c->fs->base, 0755) != 0)
            SKIP("%s not available (errno=%d)", c->fs->base, errno);
    }
    char *dir = ktest_mkdtemp(c->fs->base);
    if (!dir) {
        FAIL("could not create scratch dir under %s: errno=%d (%s)",
             c->fs->base, errno, strerror(errno));
        ktest_abort();
    }
    c->body(dir, c->fs);
}

static void fs_register(const char *name, fs_body body, int line)
{
    for (unsigned i = 0; i < sizeof(fses) / sizeof(fses[0]); i++) {
        struct fs_case *c = malloc(sizeof(*c));
        char *full = malloc(strlen(name) + 16);
        c->body = body;
        c->fs = &fses[i];
        sprintf(full, "%s.%s", fses[i].fs, name);
        ktest_register(full, fs_run, c, KTEST_DEFAULT_TIMEOUT * 3, line);
    }
}

#define FSTEST(name) \
    static void fst_##name(const char *dir, const struct fsx *fs); \
    __attribute__((constructor)) static void fsr_##name(void) \
    { fs_register(#name, fst_##name, __LINE__); } \
    static void fst_##name(const char *dir, const struct fsx *fs)

/* path join; leaks, but every test runs in a short-lived child */
static char *pj(const char *dir, const char *name)
{
    char *p = malloc(strlen(dir) + strlen(name) + 2);
    sprintf(p, "%s/%s", dir, name);
    return p;
}

static int create_file(const char *path, const void *data, size_t len)
{
    int fd = open(path, O_CREAT | O_WRONLY | O_EXCL, 0644);
    if (fd < 0) {
        FAIL("create %s: errno=%d (%s)", path, errno, strerror(errno));
        ktest_abort();
    }
    if (len && ktest_write_all(fd, data, len) != (ssize_t)len) {
        FAIL("write %s: errno=%d (%s)", path, errno, strerror(errno));
        ktest_abort();
    }
    close(fd);
    return 0;
}

static ssize_t slurp(const char *path, char *buf, size_t size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    ssize_t n = ktest_read_all(fd, buf, size);
    close(fd);
    return n;
}

static bool dir_contains_case(const char *dir, const char *name, bool nocase)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;
    struct dirent *e;
    bool found = false;
    while ((e = readdir(d)))
        if ((nocase ? strcasecmp : strcmp)(e->d_name, name) == 0)
            found = true;
    closedir(d);
    return found;
}

static bool dir_contains(const char *dir, const char *name)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;
    struct dirent *e;
    bool found = false;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, name) == 0)
            found = true;
    closedir(d);
    return found;
}

/* ================= basic I/O ================= */

FSTEST(create_write_read)
{
    char *f = pj(dir, "file.txt");
    const char msg[] = "hello, filesystem\n";
    int fd = ASSERT_OK(open(f, O_CREAT | O_RDWR, 0644));
    EXPECT_EQ(write(fd, msg, sizeof(msg) - 1), sizeof(msg) - 1);
    EXPECT_EQ(lseek(fd, 0, SEEK_SET), 0);
    char buf[64] = {0};
    EXPECT_EQ(read(fd, buf, sizeof(buf)), sizeof(msg) - 1);
    EXPECT_STREQ(buf, msg);
    EXPECT_OK(close(fd));
    /* and through a fresh open */
    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(slurp(f, buf, sizeof(buf)), sizeof(msg) - 1);
    EXPECT_STREQ(buf, msg);
}

FSTEST(empty_file)
{
    char *f = pj(dir, "empty");
    create_file(f, NULL, 0);
    struct stat st;
    ASSERT_OK(stat(f, &st));
    EXPECT_EQ(st.st_size, 0);
    char buf[4];
    int fd = ASSERT_OK(open(f, O_RDONLY));
    EXPECT_EQ(read(fd, buf, sizeof(buf)), 0);
    close(fd);
}

FSTEST(open_nonexistent_enoent)
{
    EXPECT_ERR(open(pj(dir, "nope"), O_RDONLY), ENOENT);
    EXPECT_ERR(open(pj(dir, "nodir/file"), O_RDONLY), ENOENT);
    EXPECT_ERR(open(pj(dir, "nodir/file"), O_CREAT | O_WRONLY, 0644), ENOENT);
}

FSTEST(o_excl_eexist)
{
    char *f = pj(dir, "excl");
    create_file(f, "x", 1);
    EXPECT_ERR(open(f, O_CREAT | O_EXCL | O_WRONLY, 0644), EEXIST);
}

FSTEST(o_creat_existing_keeps_data)
{
    char *f = pj(dir, "keep");
    create_file(f, "keepme", 6);
    int fd = ASSERT_OK(open(f, O_CREAT | O_RDWR, 0644));
    char buf[8] = {0};
    EXPECT_EQ(read(fd, buf, 8), 6);
    EXPECT_STREQ(buf, "keepme");
    close(fd);
}

FSTEST(o_append)
{
    char *f = pj(dir, "append");
    create_file(f, "abc", 3);
    int fd = ASSERT_OK(open(f, O_WRONLY | O_APPEND));
    EXPECT_EQ(lseek(fd, 0, SEEK_SET), 0);
    EXPECT_EQ(write(fd, "def", 3), 3);
    close(fd);
    char buf[16] = {0};
    EXPECT_EQ(slurp(f, buf, sizeof(buf)), 6);
    EXPECT_STREQ(buf, "abcdef");
}

FSTEST(o_trunc)
{
    char *f = pj(dir, "trunc");
    create_file(f, "a long line of text", 19);
    int fd = ASSERT_OK(open(f, O_WRONLY | O_TRUNC));
    struct stat st;
    EXPECT_OK(fstat(fd, &st));
    EXPECT_EQ(st.st_size, 0);
    EXPECT_EQ(write(fd, "short", 5), 5);
    close(fd);
    char buf[32] = {0};
    EXPECT_EQ(slurp(f, buf, sizeof(buf)), 5);
    EXPECT_STREQ(buf, "short");
}

FSTEST(lseek_whence)
{
    char *f = pj(dir, "seek");
    create_file(f, "0123456789", 10);
    int fd = ASSERT_OK(open(f, O_RDONLY));
    char c;
    EXPECT_EQ(lseek(fd, 3, SEEK_SET), 3);
    EXPECT_EQ(read(fd, &c, 1), 1);
    EXPECT_EQ(c, '3');
    EXPECT_EQ(lseek(fd, 2, SEEK_CUR), 6);
    EXPECT_EQ(read(fd, &c, 1), 1);
    EXPECT_EQ(c, '6');
    EXPECT_EQ(lseek(fd, -1, SEEK_END), 9);
    EXPECT_EQ(read(fd, &c, 1), 1);
    EXPECT_EQ(c, '9');
    EXPECT_EQ(read(fd, &c, 1), 0); /* EOF */
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 10);
    EXPECT_EQ(lseek(fd, 100, SEEK_SET), 100); /* seeking past EOF is legal */
    EXPECT_EQ(read(fd, &c, 1), 0);
    EXPECT_ERR(lseek(fd, -1, SEEK_SET), EINVAL);
    EXPECT_ERR(lseek(fd, 0, 42), EINVAL);
    close(fd);
}

FSTEST(write_past_eof_hole)
{
    char *f = pj(dir, "hole");
    int fd = ASSERT_OK(open(f, O_CREAT | O_RDWR, 0644));
    EXPECT_EQ(write(fd, "A", 1), 1);
    EXPECT_EQ(lseek(fd, 10000, SEEK_SET), 10000);
    EXPECT_EQ(write(fd, "B", 1), 1);
    struct stat st;
    EXPECT_OK(fstat(fd, &st));
    EXPECT_EQ(st.st_size, 10001);
    char *buf = calloc(1, 10001);
    EXPECT_EQ(pread(fd, buf, 10001, 0), 10001);
    EXPECT_EQ(buf[0], 'A');
    EXPECT_EQ(buf[10000], 'B');
    for (int i = 1; i < 10000; i++)
        if (buf[i] != 0) {
            FAIL("hole byte %d = 0x%x", i, (unsigned char)buf[i]);
            break;
        }
    close(fd);
}

FSTEST(overwrite_middle)
{
    char *f = pj(dir, "ow");
    create_file(f, "aaaaaaaaaa", 10);
    int fd = ASSERT_OK(open(f, O_RDWR));
    lseek(fd, 4, SEEK_SET);
    EXPECT_EQ(write(fd, "XY", 2), 2);
    close(fd);
    char buf[16] = {0};
    EXPECT_EQ(slurp(f, buf, sizeof(buf)), 10);
    EXPECT_STREQ(buf, "aaaaXYaaaa");
}

FSTEST(small_appends)
{
    char *f = pj(dir, "appends");
    int fd = ASSERT_OK(open(f, O_CREAT | O_WRONLY | O_APPEND, 0644));
    char rec[13];
    for (int i = 0; i < 1000; i++) {
        snprintf(rec, sizeof(rec), "%011d\n", i);
        if (write(fd, rec, 12) != 12) {
            FAIL("append %d failed errno=%d", i, errno);
            break;
        }
    }
    close(fd);
    struct stat st;
    ASSERT_OK(stat(f, &st));
    EXPECT_EQ(st.st_size, 12000);
    char *buf = malloc(12001);
    EXPECT_EQ(slurp(f, buf, 12000), 12000);
    for (int i = 0; i < 1000; i++) {
        snprintf(rec, sizeof(rec), "%011d\n", i);
        if (memcmp(buf + i * 12, rec, 12) != 0) {
            FAIL("record %d corrupt", i);
            break;
        }
    }
}

static void big_file(const char *dir, size_t size, size_t chunk)
{
    char *f = pj(dir, "big");
    char *buf = malloc(size);
    ASSERT_PTR(buf);
    ktest_fill(buf, size, 7, 0);
    int fd = ASSERT_OK(open(f, O_CREAT | O_WRONLY, 0644));
    for (size_t off = 0; off < size; off += chunk) {
        size_t n = size - off < chunk ? size - off : chunk;
        if (ktest_write_all(fd, buf + off, n) != (ssize_t)n) {
            FAIL("write at %zu failed errno=%d", off, errno);
            ktest_abort();
        }
    }
    close(fd);
    struct stat st;
    ASSERT_OK(stat(f, &st));
    EXPECT_EQ(st.st_size, size);
    memset(buf, 0, size);
    fd = ASSERT_OK(open(f, O_RDONLY));
    EXPECT_EQ(ktest_read_all(fd, buf, size), size);
    close(fd);
    size_t bad = ktest_check(buf, size, 7, 0);
    if (bad != (size_t)-1)
        FAIL("data mismatch at offset %zu of %zu", bad, size);
    free(buf);
}

FSTEST(file_1mb_integrity)
{
    big_file(dir, 1 << 20, 65536);
}

FSTEST(file_odd_chunks_integrity)
{
    /* chunk size not aligned to sectors/clusters */
    big_file(dir, 300000, 4097);
}

FSTEST(read_across_boundaries)
{
    char *f = pj(dir, "bound");
    size_t size = 70000;
    char *buf = malloc(size);
    ktest_fill(buf, size, 9, 0);
    create_file(f, buf, size);
    int fd = ASSERT_OK(open(f, O_RDONLY));
    /* reads straddling 512/4096 boundaries */
    size_t offs[] = { 510, 4090, 8191, 32767, 65530 };
    for (unsigned i = 0; i < sizeof(offs) / sizeof(offs[0]); i++) {
        char tmp[20];
        EXPECT_EQ(pread(fd, tmp, 20, offs[i]), 20);
        if (ktest_check(tmp, 20, 9, offs[i]) != (size_t)-1)
            FAIL("mismatch reading at %zu", offs[i]);
    }
    close(fd);
}

/* ================= metadata ================= */

FSTEST(stat_regular_file)
{
    char *f = pj(dir, "st");
    create_file(f, "12345", 5);
    struct stat st, fst;
    ASSERT_OK(stat(f, &st));
    EXPECT_TRUE(S_ISREG(st.st_mode));
    EXPECT_EQ(st.st_size, 5);
    EXPECT_GE(st.st_nlink, 1);
    int fd = ASSERT_OK(open(f, O_RDONLY));
    ASSERT_OK(fstat(fd, &fst));
    EXPECT_EQ(fst.st_ino, st.st_ino);
    EXPECT_EQ(fst.st_dev, st.st_dev);
    EXPECT_EQ(fst.st_size, st.st_size);
    close(fd);
}

FSTEST(stat_directory)
{
    struct stat st;
    ASSERT_OK(stat(dir, &st));
    EXPECT_TRUE(S_ISDIR(st.st_mode));
    ASSERT_OK(stat(fs->base, &st));
    EXPECT_TRUE(S_ISDIR(st.st_mode));
}

FSTEST(stat_size_tracks_writes)
{
    char *f = pj(dir, "grow");
    int fd = ASSERT_OK(open(f, O_CREAT | O_RDWR, 0644));
    struct stat st;
    char buf[1000] = {0};
    for (int i = 1; i <= 5; i++) {
        write(fd, buf, sizeof(buf));
        fstat(fd, &st);
        EXPECT_EQ(st.st_size, i * 1000);
    }
    close(fd);
}

FSTEST(distinct_inodes)
{
    char *a = pj(dir, "a"), *b = pj(dir, "b");
    create_file(a, "1", 1);
    create_file(b, "2", 1);
    struct stat sa, sb;
    ASSERT_OK(stat(a, &sa));
    ASSERT_OK(stat(b, &sb));
    EXPECT_NE(sa.st_ino, sb.st_ino);
}

FSTEST(mtime_updates)
{
    char *f = pj(dir, "mt");
    time_t before = time(NULL);
    create_file(f, "x", 1);
    struct stat st;
    ASSERT_OK(stat(f, &st));
    EXPECT_GE(st.st_mtime, before - 2);
    EXPECT_LE(st.st_mtime, time(NULL) + 2);
}

/* ================= directories ================= */

FSTEST(mkdir_rmdir)
{
    char *d = pj(dir, "sub");
    EXPECT_OK(mkdir(d, 0755));
    struct stat st;
    EXPECT_OK(stat(d, &st));
    EXPECT_TRUE(S_ISDIR(st.st_mode));
    EXPECT_OK(rmdir(d));
    EXPECT_ERR(stat(d, &st), ENOENT);
}

FSTEST(mkdir_existing_eexist)
{
    char *d = pj(dir, "sub");
    EXPECT_OK(mkdir(d, 0755));
    EXPECT_ERR(mkdir(d, 0755), EEXIST);
    char *f = pj(dir, "file");
    create_file(f, "x", 1);
    EXPECT_ERR(mkdir(f, 0755), EEXIST);
}

FSTEST(rmdir_nonempty)
{
    char *d = pj(dir, "full");
    ASSERT_OK(mkdir(d, 0755));
    create_file(pj(d, "f"), "x", 1);
    errno = 0;
    EXPECT_EQ(rmdir(d), -1);
    EXPECT_TRUE(errno == ENOTEMPTY || errno == EEXIST);
}

FSTEST(rmdir_on_file_enotdir)
{
    char *f = pj(dir, "f");
    create_file(f, "x", 1);
    EXPECT_ERR(rmdir(f), ENOTDIR);
    EXPECT_ERR(rmdir(pj(dir, "missing")), ENOENT);
}

FSTEST(nested_dirs)
{
    char path[512];
    strcpy(path, dir);
    for (int i = 0; i < 10; i++) {
        char comp[16];
        snprintf(comp, sizeof(comp), "/d%d", i);
        strcat(path, comp);
        if (mkdir(path, 0755) != 0) {
            FAIL("mkdir %s errno=%d", path, errno);
            ktest_abort();
        }
    }
    strcat(path, "/leaf");
    create_file(path, "deep", 4);
    char buf[8] = {0};
    EXPECT_EQ(slurp(path, buf, sizeof(buf)), 4);
    EXPECT_STREQ(buf, "deep");
}

FSTEST(readdir_lists_entries)
{
    const char *names[] = { "alpha", "beta", "gamma.txt", "Delta" };
    for (unsigned i = 0; i < 4; i++)
        create_file(pj(dir, names[i]), "x", 1);
    ASSERT_OK(mkdir(pj(dir, "subdir"), 0755));

    DIR *d = opendir(dir);
    ASSERT_TRUE(d != NULL);
    int seen = 0, dots = 0, other = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        int hit = 0;
        for (unsigned i = 0; i < 4; i++)
            if (strcmp(e->d_name, names[i]) == 0) {
                seen |= 1 << i;
                hit = 1;
            }
        if (strcmp(e->d_name, "subdir") == 0) {
            seen |= 1 << 4;
            hit = 1;
        }
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            dots++;
            hit = 1;
        }
        if (!hit) {
            other++;
            ktest_log("unexpected entry '%s'", e->d_name);
        }
    }
    closedir(d);
    EXPECT_EQ(seen, 0x1f);
    EXPECT_EQ(dots, 2);
    EXPECT_EQ(other, 0);
}

FSTEST(readdir_empty_dir)
{
    DIR *d = opendir(dir);
    ASSERT_TRUE(d != NULL);
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
            n++;
    closedir(d);
    EXPECT_EQ(n, 0);
}

FSTEST(readdir_d_type)
{
    create_file(pj(dir, "reg"), "x", 1);
    ASSERT_OK(mkdir(pj(dir, "dir"), 0755));
    DIR *d = opendir(dir);
    ASSERT_TRUE(d != NULL);
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, "reg"))
            EXPECT_EQ(e->d_type, DT_REG);
        if (!strcmp(e->d_name, "dir"))
            EXPECT_EQ(e->d_type, DT_DIR);
    }
    closedir(d);
}

FSTEST(many_files_in_dir)
{
    enum { N = 200 };
    char name[32];
    for (int i = 0; i < N; i++) {
        snprintf(name, sizeof(name), "file_%03d", i);
        char *p = pj(dir, name);
        int fd = open(p, O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (fd < 0) {
            FAIL("create #%d failed errno=%d (%s)", i, errno, strerror(errno));
            ktest_abort();
        }
        dprintf(fd, "%d", i);
        close(fd);
    }
    static unsigned char seen[N];
    DIR *d = opendir(dir);
    ASSERT_TRUE(d != NULL);
    struct dirent *e;
    int count = 0, dups = 0, total = 0;
    while ((e = readdir(d)) && total++ < 10 * N) {
        int n;
        if (sscanf(e->d_name, "file_%d", &n) == 1 && n >= 0 && n < N) {
            if (seen[n]) {
                if (dups++ < 3)
                    FAIL("duplicate entry %s", e->d_name);
                continue;
            }
            seen[n] = 1;
            count++;
        }
    }
    closedir(d);
    if (dups)
        FAIL("%d duplicate entries in total", dups);
    EXPECT_EQ(count, N);
    /* spot-check contents via lookup */
    for (int i = 0; i < N; i += 37) {
        snprintf(name, sizeof(name), "file_%03d", i);
        char buf[8] = {0};
        slurp(pj(dir, name), buf, sizeof(buf));
        EXPECT_EQ(atoi(buf), i);
    }
}

FSTEST(long_filename)
{
    char name[101];
    memset(name, 'n', 100);
    name[100] = 0;
    memcpy(name, "LongName_", 9);
    char *f = pj(dir, name);
    create_file(f, "long", 4);
    EXPECT_TRUE(dir_contains(dir, name));
    char buf[8] = {0};
    EXPECT_EQ(slurp(f, buf, sizeof(buf)), 4);
}

FSTEST(filename_case_and_dots)
{
    const char *names[] = { "MixedCase.Txt", "file.tar.gz", "with space", ".hidden" };
    for (unsigned i = 0; i < 4; i++) {
        char *f = pj(dir, names[i]);
        int fd = open(f, O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (fd < 0) {
            FAIL("create '%s' errno=%d", names[i], errno);
            continue;
        }
        close(fd);
        if (!dir_contains(dir, names[i]))
            FAIL("'%s' missing from readdir (name mangled?)", names[i]);
        struct stat st;
        if (stat(f, &st) != 0)
            FAIL("stat '%s' errno=%d", names[i], errno);
    }
}

FSTEST(enotdir_component)
{
    char *f = pj(dir, "plain");
    create_file(f, "x", 1);
    EXPECT_ERR(open(pj(f, "child"), O_RDONLY), ENOTDIR);
    EXPECT_ERR(mkdir(pj(f, "child"), 0755), ENOTDIR);
}

/* ================= unlink / rename / truncate ================= */

FSTEST(unlink_file)
{
    char *f = pj(dir, "gone");
    create_file(f, "x", 1);
    EXPECT_OK(unlink(f));
    EXPECT_ERR(open(f, O_RDONLY), ENOENT);
    EXPECT_TRUE(!dir_contains(dir, "gone"));
    EXPECT_ERR(unlink(f), ENOENT);
}

FSTEST(unlink_dir_fails)
{
    char *d = pj(dir, "d");
    ASSERT_OK(mkdir(d, 0755));
    errno = 0;
    EXPECT_EQ(unlink(d), -1);
    EXPECT_TRUE(errno == EISDIR || errno == EPERM);
}

FSTEST(unlink_open_file_readable)
{
    char *f = pj(dir, "ghost");
    create_file(f, "still here", 10);
    int fd = ASSERT_OK(open(f, O_RDONLY));
    ASSERT_OK(unlink(f));
    char buf[16] = {0};
    EXPECT_EQ(read(fd, buf, sizeof(buf)), 10);
    EXPECT_STREQ(buf, "still here");
    close(fd);
}

FSTEST(unlink_then_recreate)
{
    char *f = pj(dir, "again");
    create_file(f, "first version", 13);
    ASSERT_OK(unlink(f));
    create_file(f, "2nd", 3);
    char buf[16] = {0};
    EXPECT_EQ(slurp(f, buf, sizeof(buf)), 3);
    EXPECT_STREQ(buf, "2nd");
}

FSTEST(rename_file)
{
    char *a = pj(dir, "old"), *b = pj(dir, "new");
    create_file(a, "data", 4);
    EXPECT_OK(rename(a, b));
    EXPECT_ERR(open(a, O_RDONLY), ENOENT);
    char buf[8] = {0};
    EXPECT_EQ(slurp(b, buf, sizeof(buf)), 4);
    EXPECT_STREQ(buf, "data");
}

FSTEST(rename_overwrites)
{
    char *a = pj(dir, "src"), *b = pj(dir, "dst");
    create_file(a, "new", 3);
    create_file(b, "old!!", 5);
    EXPECT_OK(rename(a, b));
    char buf[8] = {0};
    EXPECT_EQ(slurp(b, buf, sizeof(buf)), 3);
    EXPECT_STREQ(buf, "new");
}

FSTEST(rename_across_dirs)
{
    char *d1 = pj(dir, "d1"), *d2 = pj(dir, "d2");
    ASSERT_OK(mkdir(d1, 0755));
    ASSERT_OK(mkdir(d2, 0755));
    create_file(pj(d1, "f"), "mv", 2);
    EXPECT_OK(rename(pj(d1, "f"), pj(d2, "g")));
    EXPECT_TRUE(!dir_contains(d1, "f"));
    EXPECT_TRUE(dir_contains(d2, "g"));
    /* rename a directory */
    EXPECT_OK(rename(d1, pj(dir, "d3")));
    struct stat st;
    EXPECT_OK(stat(pj(dir, "d3"), &st));
}

FSTEST(ftruncate_shrink_grow)
{
    char *f = pj(dir, "ft");
    create_file(f, "0123456789", 10);
    int fd = ASSERT_OK(open(f, O_RDWR));
    struct stat st;
    EXPECT_OK(ftruncate(fd, 4));
    fstat(fd, &st);
    EXPECT_EQ(st.st_size, 4);
    EXPECT_OK(ftruncate(fd, 8));
    fstat(fd, &st);
    EXPECT_EQ(st.st_size, 8);
    char buf[8];
    EXPECT_EQ(pread(fd, buf, 8, 0), 8);
    EXPECT_EQ(memcmp(buf, "0123\0\0\0\0", 8), 0);
    close(fd);
}

/* shrink across many clusters, then reuse the freed space */
FSTEST(ftruncate_multi_cluster)
{
    enum { BIG = 64 * 1024, CUT = 5000 };
    static unsigned char pat[BIG], buf[BIG];
    for (int i = 0; i < BIG; i++)
        pat[i] = (unsigned char)(i * 7 + 3);
    char *f = pj(dir, "big");
    create_file(f, pat, BIG);
    int fd = ASSERT_OK(open(f, O_RDWR));
    struct stat st;

    EXPECT_OK(ftruncate(fd, CUT));
    fstat(fd, &st);
    EXPECT_EQ(st.st_size, CUT);
    EXPECT_EQ(pread(fd, buf, BIG, 0), CUT);
    EXPECT_EQ(memcmp(buf, pat, CUT), 0);

    /* the freed clusters go to another file; ours must not see its data */
    char *g = pj(dir, "other");
    static unsigned char ones[BIG];
    memset(ones, 0xAA, BIG);
    create_file(g, ones, BIG);

    /* regrow: the old tail must read back as zeros, not the old pattern */
    EXPECT_OK(ftruncate(fd, BIG));
    EXPECT_EQ(pread(fd, buf, BIG, 0), BIG);
    EXPECT_EQ(memcmp(buf, pat, CUT), 0);
    int nonzero = 0;
    for (int i = CUT; i < BIG; i++)
        nonzero += buf[i] != 0;
    EXPECT_EQ(nonzero, 0);

    EXPECT_EQ(slurp(g, (char *)buf, BIG), BIG);
    EXPECT_EQ(memcmp(buf, ones, BIG), 0);

    EXPECT_OK(ftruncate(fd, 0));
    fstat(fd, &st);
    EXPECT_EQ(st.st_size, 0);
    EXPECT_EQ(pwrite(fd, "x", 1, 0), 1);
    EXPECT_EQ(pread(fd, buf, BIG, 0), 1);
    close(fd);
}

FSTEST(truncate_path)
{
    char *f = pj(dir, "tp");
    create_file(f, "0123456789", 10);
    EXPECT_OK(truncate(f, 3));
    struct stat st;
    stat(f, &st);
    EXPECT_EQ(st.st_size, 3);
}

/* ================= links ================= */

// FAT has no links; Linux vfat returns EPERM too
static void skip_if_no_links(const struct fsx *fs, int r)
{
    if (r != 0 && errno == EPERM && !strcmp(fs->fs, "fat32"))
        SKIP("FAT does not support links");
}

FSTEST(hard_link)
{
    char *a = pj(dir, "orig"), *b = pj(dir, "alias");
    create_file(a, "shared", 6);
    int lr = link(a, b);
    skip_if_no_links(fs, lr);
    EXPECT_OK(lr);
    struct stat sa, sb;
    ASSERT_OK(stat(a, &sa));
    ASSERT_OK(stat(b, &sb));
    EXPECT_EQ(sa.st_ino, sb.st_ino);
    EXPECT_EQ(sa.st_nlink, 2);
    EXPECT_OK(unlink(a));
    char buf[8] = {0};
    EXPECT_EQ(slurp(b, buf, sizeof(buf)), 6);
    EXPECT_STREQ(buf, "shared");
    stat(b, &sb);
    EXPECT_EQ(sb.st_nlink, 1);
}

FSTEST(symlink_readlink)
{
    char *target = pj(dir, "target"), *ln = pj(dir, "ln");
    create_file(target, "via link", 8);
    int sr = symlink(target, ln);
    skip_if_no_links(fs, sr);
    EXPECT_OK(sr);
    char buf[256] = {0};
    ssize_t n = readlink(ln, buf, sizeof(buf) - 1);
    EXPECT_EQ(n, (ssize_t)strlen(target));
    EXPECT_STREQ(buf, target);
    struct stat st;
    EXPECT_OK(lstat(ln, &st));
    EXPECT_TRUE(S_ISLNK(st.st_mode));
    EXPECT_OK(stat(ln, &st));
    EXPECT_TRUE(S_ISREG(st.st_mode));
    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(slurp(ln, buf, sizeof(buf)), 8);
    EXPECT_STREQ(buf, "via link");
}

FSTEST(symlink_relative_and_dangling)
{
    create_file(pj(dir, "t"), "rel", 3);
    char *ln = pj(dir, "rel_ln");
    int sr = symlink("t", ln);
    skip_if_no_links(fs, sr);
    EXPECT_OK(sr);
    char buf[8] = {0};
    EXPECT_EQ(slurp(ln, buf, sizeof(buf)), 3);
    char *dangling = pj(dir, "dangle");
    EXPECT_OK(symlink("does-not-exist", dangling));
    EXPECT_ERR(open(dangling, O_RDONLY), ENOENT);
    struct stat st;
    EXPECT_OK(lstat(dangling, &st));
    EXPECT_ERR(readlink(pj(dir, "t"), buf, sizeof(buf)), EINVAL);
}

/* ================= paths / cwd ================= */

FSTEST(chdir_relative_paths)
{
    ASSERT_OK(mkdir(pj(dir, "cwd"), 0755));
    ASSERT_OK(chdir(pj(dir, "cwd")));
    char cwd[256];
    ASSERT_TRUE(getcwd(cwd, sizeof(cwd)) != NULL);
    EXPECT_STREQ(cwd, pj(dir, "cwd"));
    create_file("rel.txt", "r", 1);
    struct stat st;
    EXPECT_OK(stat(pj(dir, "cwd/rel.txt"), &st));
    EXPECT_OK(stat("./rel.txt", &st));
    EXPECT_OK(stat("../cwd/rel.txt", &st));
    ASSERT_OK(chdir(".."));
    getcwd(cwd, sizeof(cwd));
    EXPECT_STREQ(cwd, dir);
    EXPECT_ERR(chdir("does-not-exist"), ENOENT);
    create_file("plainfile", "x", 1);
    EXPECT_ERR(chdir("plainfile"), ENOTDIR);
}

FSTEST(dotdot_resolution)
{
    ASSERT_OK(mkdir(pj(dir, "a"), 0755));
    ASSERT_OK(mkdir(pj(dir, "a/b"), 0755));
    create_file(pj(dir, "top"), "t", 1);
    struct stat st;
    EXPECT_OK(stat(pj(dir, "a/b/../../top"), &st));
    EXPECT_OK(stat(pj(dir, "a/./b/./.."), &st));
    EXPECT_TRUE(S_ISDIR(st.st_mode));
    EXPECT_OK(stat(pj(dir, "a//b"), &st));
    EXPECT_OK(stat(pj(dir, "a/b/"), &st));
    EXPECT_ERR(stat(pj(dir, "top/"), &st), ENOTDIR);
}

FSTEST(dotdot_across_mount)
{
    /* ".." from the root of a mounted fs must go to the parent fs */
    char *p = pj(fs->base, "../tests/helper");
    struct stat st;
    EXPECT_OK(stat(p, &st));
    ASSERT_OK(chdir(fs->base));
    ASSERT_OK(chdir(".."));
    char cwd[64];
    getcwd(cwd, sizeof(cwd));
    EXPECT_STREQ(cwd, "/");
}

FSTEST(access_modes)
{
    char *f = pj(dir, "acc");
    create_file(f, "x", 1);
    EXPECT_OK(access(f, F_OK));
    EXPECT_OK(access(f, R_OK | W_OK));
    EXPECT_OK(access(dir, X_OK));
    EXPECT_ERR(access(pj(dir, "missing"), F_OK), ENOENT);
}

FSTEST(directory_io_errors)
{
    EXPECT_ERR(open(dir, O_WRONLY), EISDIR);
    EXPECT_ERR(open(dir, O_RDWR), EISDIR);
    int fd = ASSERT_OK(open(dir, O_RDONLY));
    char buf[8];
    EXPECT_ERR(read(fd, buf, sizeof(buf)), EISDIR);
    close(fd);
    char *f = pj(dir, "notadir");
    create_file(f, "x", 1);
    EXPECT_ERR(open(f, O_RDONLY | O_DIRECTORY), ENOTDIR);
    EXPECT_TRUE(opendir(f) == NULL);
}

/* ================= fd semantics ================= */

FSTEST(access_mode_enforced)
{
    char *f = pj(dir, "ro");
    create_file(f, "abc", 3);
    int fd = ASSERT_OK(open(f, O_RDONLY));
    EXPECT_ERR(write(fd, "x", 1), EBADF);
    close(fd);
    fd = ASSERT_OK(open(f, O_WRONLY));
    char c;
    EXPECT_ERR(read(fd, &c, 1), EBADF);
    close(fd);
}

FSTEST(dup_shares_offset)
{
    char *f = pj(dir, "dup");
    create_file(f, "abcdef", 6);
    int fd = ASSERT_OK(open(f, O_RDONLY));
    int fd2 = ASSERT_OK(dup(fd));
    EXPECT_NE(fd, fd2);
    char c;
    read(fd, &c, 1);
    read(fd, &c, 1);
    EXPECT_EQ(read(fd2, &c, 1), 1);
    EXPECT_EQ(c, 'c');
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 3);
    close(fd);
    EXPECT_EQ(read(fd2, &c, 1), 1); /* still usable after closing original */
    EXPECT_EQ(c, 'd');
    close(fd2);
}

FSTEST(dup2_replaces)
{
    char *a = pj(dir, "a"), *b = pj(dir, "b");
    create_file(a, "A", 1);
    create_file(b, "B", 1);
    int fa = ASSERT_OK(open(a, O_RDONLY));
    int fb = ASSERT_OK(open(b, O_RDONLY));
    EXPECT_EQ(dup2(fa, fb), fb);
    char c = 0;
    EXPECT_EQ(read(fb, &c, 1), 1);
    EXPECT_EQ(c, 'A');
    EXPECT_EQ(dup2(fa, fa), fa);
    EXPECT_EQ(dup2(fa, 50), 50);
    EXPECT_OK(fcntl(50, F_GETFD));
    EXPECT_ERR(dup2(999999, 10), EBADF);
    close(fa);
    close(fb);
    close(50);
}

FSTEST(lowest_fd_reused)
{
    char *f = pj(dir, "fd");
    create_file(f, "x", 1);
    int a = ASSERT_OK(open(f, O_RDONLY));
    int b = ASSERT_OK(open(f, O_RDONLY));
    int c = ASSERT_OK(open(f, O_RDONLY));
    EXPECT_EQ(b, a + 1);
    EXPECT_EQ(c, b + 1);
    close(a);
    int d = ASSERT_OK(open(f, O_RDONLY));
    EXPECT_EQ(d, a);
    close(b);
    close(c);
    close(d);
}

FSTEST(many_open_fds)
{
    char *f = pj(dir, "many");
    create_file(f, "x", 1);
    int fds[64];
    int n = 0;
    for (; n < 64; n++) {
        fds[n] = open(f, O_RDONLY);
        if (fds[n] < 0) {
            FAIL("open #%d failed errno=%d (fd table growth?)", n, errno);
            break;
        }
    }
    for (int i = 0; i < n; i++) {
        char c;
        if (read(fds[i], &c, 1) != 1)
            FAIL("read on fd %d failed", fds[i]);
        close(fds[i]);
    }
}

FSTEST(close_ebadf)
{
    EXPECT_ERR(close(9999), EBADF);
    EXPECT_ERR(close(-1), EBADF);
    char *f = pj(dir, "c");
    create_file(f, "x", 1);
    int fd = ASSERT_OK(open(f, O_RDONLY));
    EXPECT_OK(close(fd));
    EXPECT_ERR(close(fd), EBADF);
    char c;
    EXPECT_ERR(read(fd, &c, 1), EBADF);
}

FSTEST(pread_pwrite_keep_offset)
{
    char *f = pj(dir, "pp");
    create_file(f, "0123456789", 10);
    int fd = ASSERT_OK(open(f, O_RDWR));
    lseek(fd, 2, SEEK_SET);
    char buf[4] = {0};
    EXPECT_EQ(pread(fd, buf, 3, 5), 3);
    EXPECT_EQ(memcmp(buf, "567", 3), 0);
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 2);
    EXPECT_EQ(pwrite(fd, "XY", 2, 8), 2);
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 2);
    char all[11] = {0};
    EXPECT_EQ(pread(fd, all, 10, 0), 10);
    EXPECT_STREQ(all, "01234567XY");
    close(fd);
}

FSTEST(readv_writev)
{
    char *f = pj(dir, "iov");
    int fd = ASSERT_OK(open(f, O_CREAT | O_RDWR, 0644));
    struct iovec w[3] = {
        { "hello ", 6 }, { "vectored ", 9 }, { "world", 5 },
    };
    EXPECT_EQ(writev(fd, w, 3), 20);
    lseek(fd, 0, SEEK_SET);
    char a[5], b[10], c[6] = {0};
    struct iovec r[3] = { { a, 5 }, { b, 10 }, { c, 5 } };
    EXPECT_EQ(readv(fd, r, 3), 20);
    EXPECT_EQ(memcmp(a, "hello", 5), 0);
    EXPECT_EQ(memcmp(b, " vectored ", 10), 0);
    EXPECT_STREQ(c, "world");
    close(fd);
}

FSTEST(fcntl_flags)
{
    char *f = pj(dir, "fl");
    create_file(f, "abc", 3);
    int fd = ASSERT_OK(open(f, O_RDWR));
    int fl = EXPECT_OK(fcntl(fd, F_GETFL));
    EXPECT_EQ(fl & O_ACCMODE, O_RDWR);
    EXPECT_OK(fcntl(fd, F_SETFL, fl | O_APPEND));
    fl = fcntl(fd, F_GETFL);
    EXPECT_TRUE(fl & O_APPEND);
    lseek(fd, 0, SEEK_SET);
    write(fd, "d", 1);
    char buf[8] = {0};
    EXPECT_EQ(pread(fd, buf, 8, 0), 4);
    EXPECT_STREQ(buf, "abcd");
    close(fd);
}

FSTEST(fcntl_dupfd)
{
    char *f = pj(dir, "dfd");
    create_file(f, "x", 1);
    int fd = ASSERT_OK(open(f, O_RDONLY));
    int d = EXPECT_OK(fcntl(fd, F_DUPFD, 20));
    EXPECT_GE(d, 20);
    int d2 = EXPECT_OK(fcntl(fd, F_DUPFD_CLOEXEC, 30));
    EXPECT_GE(d2, 30);
    EXPECT_TRUE(fcntl(d2, F_GETFD) & FD_CLOEXEC);
    EXPECT_TRUE(!(fcntl(d, F_GETFD) & FD_CLOEXEC));
    close(fd);
    close(d);
    close(d2);
}

static int helper_fd_open(int fd)
{
    char num[16];
    snprintf(num, sizeof(num), "%d", fd);
    char *argv[] = { "/tests/helper", "fdopen", num, NULL };
    int st = ktest_spawn(argv, NULL, 0);
    if (st == -1 || !WIFEXITED(st))
        return -1;
    return WEXITSTATUS(st) == 0;
}

FSTEST(cloexec_across_exec)
{
    char *f = pj(dir, "ce");
    create_file(f, "x", 1);
    int keep = ASSERT_OK(open(f, O_RDONLY));
    int drop = ASSERT_OK(open(f, O_RDONLY | O_CLOEXEC));
    EXPECT_TRUE(fcntl(drop, F_GETFD) & FD_CLOEXEC);
    EXPECT_EQ(helper_fd_open(keep), 1);
    EXPECT_EQ(helper_fd_open(drop), 0);
    int drop2 = ASSERT_OK(open(f, O_RDONLY));
    EXPECT_OK(fcntl(drop2, F_SETFD, FD_CLOEXEC));
    EXPECT_EQ(helper_fd_open(drop2), 0);
}

FSTEST(offset_shared_across_fork)
{
    char *f = pj(dir, "fork");
    create_file(f, "0123456789", 10);
    int fd = ASSERT_OK(open(f, O_RDONLY));
    pid_t pid = ASSERT_OK(fork());
    if (pid == 0) {
        char buf[4];
        read(fd, buf, 4);
        _exit(0);
    }
    waitpid(pid, NULL, 0);
    EXPECT_EQ(lseek(fd, 0, SEEK_CUR), 4);
    close(fd);
}

FSTEST(two_fds_see_writes)
{
    char *f = pj(dir, "coh");
    create_file(f, "..........", 10);
    int w = ASSERT_OK(open(f, O_WRONLY));
    int r = ASSERT_OK(open(f, O_RDONLY));
    EXPECT_EQ(pwrite(w, "HELLO", 5, 2), 5);
    char buf[11] = {0};
    EXPECT_EQ(pread(r, buf, 10, 0), 10);
    EXPECT_STREQ(buf, "..HELLO...");
    /* independent offsets */
    EXPECT_EQ(lseek(w, 0, SEEK_CUR), 0);
    close(w);
    close(r);
}

FSTEST(fsync_ok)
{
    char *f = pj(dir, "sync");
    int fd = ASSERT_OK(open(f, O_CREAT | O_WRONLY, 0644));
    write(fd, "x", 1);
    EXPECT_OK(fsync(fd));
    close(fd);
}

/* ================= non-parameterized ================= */

TEST(dev_null_semantics)
{
    int fd = ASSERT_OK(open("/dev/null", O_RDWR));
    EXPECT_EQ(write(fd, "discard me", 10), 10);
    char c;
    EXPECT_EQ(read(fd, &c, 1), 0);
    close(fd);
    /* writes must not accumulate */
    struct stat st;
    EXPECT_OK(stat("/dev/null", &st));
    EXPECT_TRUE(S_ISCHR(st.st_mode));
    EXPECT_EQ(st.st_size, 0);
}

TEST(dev_zero_reads_zeros)
{
    int fd = ASSERT_OK(open("/dev/zero", O_RDONLY));
    char buf[256];
    memset(buf, 0xff, sizeof(buf));
    EXPECT_EQ(read(fd, buf, sizeof(buf)), sizeof(buf));
    for (unsigned i = 0; i < sizeof(buf); i++)
        if (buf[i]) {
            FAIL("byte %u = 0x%x", i, (unsigned char)buf[i]);
            break;
        }
    close(fd);
}

TEST(root_dir_listing)
{
    /* case-insensitive: the test image is built with mtools, which stores
       short lowercase names as 8.3 + NT case flags rather than LFN entries
       (fat32 name round-trips are covered by readdir_lists_entries) */
    const char *want[] = { "bin", "sbin", "lib", "tests", "tmp", "dev", "boot" };
    bool missing = false;
    for (unsigned i = 0; i < sizeof(want) / sizeof(want[0]); i++)
        if (!dir_contains_case("/", want[i], true)) {
            FAIL("'/' listing missing %s", want[i]);
            missing = true;
        }
    if (missing) {
        DIR *d = opendir("/");
        struct dirent *e;
        while (d && (e = readdir(d)))
            ktest_log("'/' has entry '%s'", e->d_name);
        if (d)
            closedir(d);
    }
}

TEST(dev_listing)
{
    const char *want[] = { "null", "zero", "tty0", "tty", "fb0", "kmsg" };
    for (unsigned i = 0; i < sizeof(want) / sizeof(want[0]); i++)
        if (!dir_contains("/dev", want[i]))
            FAIL("/dev missing %s", want[i]);
}

TEST(initial_cwd_and_root)
{
    char cwd[64];
    ASSERT_OK(chdir("/"));
    ASSERT_TRUE(getcwd(cwd, sizeof(cwd)) != NULL);
    EXPECT_STREQ(cwd, "/");
    struct stat st;
    EXPECT_OK(stat("/..", &st));
    struct stat root;
    stat("/", &root);
    EXPECT_EQ(st.st_ino, root.st_ino);
}

// "/" and "." return the root and the cwd with a reference; a missing or
// extra one underflows or leaks them, so repeat enough to trip either
TEST(root_and_dot_lookups_balanced)
{
    struct stat st, root, here;
    ASSERT_OK(stat("/", &root));
    ASSERT_OK(chdir("/tmp"));
    ASSERT_OK(stat("/tmp", &here));
    for (int i = 0; i < 2000; i++) {
        ASSERT_OK(stat("/", &st));
        ASSERT_EQ(st.st_ino, root.st_ino);
        ASSERT_OK(stat(".", &st));
        ASSERT_EQ(st.st_ino, here.st_ino);
        int fd = open("/", O_RDONLY);
        ASSERT_TRUE(fd >= 0);
        close(fd);
        fd = open(".", O_RDONLY);
        ASSERT_TRUE(fd >= 0);
        close(fd);
        ASSERT_OK(access(".", F_OK));
    }
    char cwd[64];
    ASSERT_OK(chdir("/"));
    ASSERT_OK(chdir("/tmp"));
    ASSERT_TRUE(getcwd(cwd, sizeof(cwd)) != NULL);
    EXPECT_STREQ(cwd, "/tmp");
    ASSERT_OK(chdir("."));
    ASSERT_OK(stat(".", &st));
    EXPECT_EQ(st.st_ino, here.st_ino);
}

TEST(getcwd_erange)
{
    ASSERT_OK(chdir("/tests"));
    char small[3];
    errno = 0;
    EXPECT_TRUE(getcwd(small, sizeof(small)) == NULL);
    EXPECT_EQ(errno, ERANGE);
}

TEST(empty_path_enoent)
{
    EXPECT_ERR(open("", O_RDONLY), ENOENT);
    struct stat st;
    EXPECT_ERR(stat("", &st), ENOENT);
}

TEST(tmp_is_separate_mount)
{
    struct stat r, t;
    ASSERT_OK(stat("/", &r));
    ASSERT_OK(stat("/tmp", &t));
    EXPECT_NE(r.st_dev, t.st_dev);
}

/* ---- manifest checks (generated by scripts/run-tests.sh) ---- */

static uint32_t crc_table[256];

static void crc_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i << 24;
        for (int j = 0; j < 8; j++)
            c = c & 0x80000000u ? (c << 1) ^ 0x04c11db7u : c << 1;
        crc_table[i] = c;
    }
}

/* POSIX cksum(1) CRC over the first `limit` bytes. Stops at `limit` rather
   than relying on read() returning 0 at EOF, so content is checked
   independently of EOF handling (which has its own tests). */
static int file_cksum(const char *path, size_t limit, uint32_t *crc_out, size_t *size_out)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    uint32_t crc = 0;
    size_t total = 0;
    static unsigned char buf[65536];
    ssize_t n = 0;
    while (total < limit &&
           (n = read(fd, buf, limit - total < sizeof(buf) ? limit - total : sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++)
            crc = (crc << 8) ^ crc_table[(crc >> 24) ^ buf[i]];
        total += n;
    }
    close(fd);
    if (n < 0)
        return -1;
    for (size_t len = total; len; len >>= 8)
        crc = (crc << 8) ^ crc_table[(crc >> 24) ^ (len & 0xff)];
    *crc_out = ~crc;
    *size_out = total;
    return 0;
}

static void check_manifest(const char *manifest, const char *prefix)
{
    FILE *m = fopen(manifest, "r");
    if (!m)
        SKIP("no manifest %s", manifest);
    crc_init();
    char line[512];
    int checked = 0;
    while (fgets(line, sizeof(line), m)) {
        unsigned long crc, size;
        char rel[400];
        if (sscanf(line, "%lu %lu %399s", &crc, &size, rel) != 3)
            continue;
        char path[512];
        snprintf(path, sizeof(path), "%s%s", prefix, rel);
        struct stat st;
        if (stat(path, &st) != 0) {
            FAIL("%s: stat errno=%d", path, errno);
            continue;
        }
        if ((unsigned long)st.st_size != size)
            FAIL("%s: st_size %ld, expected %lu", path, (long)st.st_size, size);
        uint32_t got;
        size_t got_size;
        if (file_cksum(path, size, &got, &got_size) != 0) {
            FAIL("%s: read errno=%d", path, errno);
            continue;
        }
        if (got_size != size || got != crc)
            FAIL("%s: read %zu bytes crc %u, expected %lu bytes crc %lu",
                 path, got_size, got, size, crc);
        checked++;
    }
    fclose(m);
    EXPECT_GT(checked, 0);
}

TEST_TIMEOUT(fat32_read_manifest, 60)
{
    check_manifest("/tests/root-manifest.txt", "");
}

static bool ext2_mounted(void)
{
    struct stat st;
    return stat("/mnt/lost+found", &st) == 0;
}

TEST_TIMEOUT(ext2_read_manifest, 60)
{
    if (!ext2_mounted())
        SKIP("ext2 not mounted at /mnt (kernel built without -DDEBUG?)");
    check_manifest("/tests/ext2-manifest.txt", "/mnt");
}

TEST(ext2_readdir)
{
    if (!ext2_mounted())
        SKIP("ext2 not mounted");
    const char *root[] = { "bin", "boot", "etc", "lib", "sbin", "usr", "lost+found" };
    for (unsigned i = 0; i < sizeof(root) / sizeof(root[0]); i++)
        if (!dir_contains("/mnt", root[i]))
            FAIL("/mnt missing %s", root[i]);
    if (!dir_contains("/mnt/bin", "echo"))
        FAIL("/mnt/bin missing echo");
    struct stat st;
    EXPECT_OK(stat("/mnt/bin", &st));
    EXPECT_TRUE(S_ISDIR(st.st_mode));
}

TEST(ext2_exec_binary)
{
    if (!ext2_mounted())
        SKIP("ext2 not mounted");
    char out[32];
    char *argv[] = { "/mnt/bin/echo", "from-ext2", NULL };
    int st = ktest_spawn(argv, out, sizeof(out));
    EXPECT_TRUE(st != -1 && WIFEXITED(st) && WEXITSTATUS(st) == 0);
    EXPECT_STREQ(out, "from-ext2\n");
}

TEST(ext2_write_support)
{
    SKIP("ext2 write support not implemented");
    if (!ext2_mounted())
        SKIP("ext2 not mounted");
    int fd = open("/mnt/ktest_write", O_CREAT | O_WRONLY, 0644);
    if (fd < 0) {
        FAIL("ext2 create failed: errno=%d (%s) -- no write support", errno, strerror(errno));
        return;
    }
    EXPECT_EQ(write(fd, "x", 1), 1);
    close(fd);
    unlink("/mnt/ktest_write");
}

TEST(ext2_missing_file)
{
    if (!ext2_mounted())
        SKIP("ext2 not mounted");
    EXPECT_ERR(open("/mnt/no/such/file", O_RDONLY), ENOENT);
    EXPECT_ERR(open("/mnt/nosuchfile", O_RDONLY), ENOENT);
}

/* A directory renamed out from under a process's cwd: getcwd and ".." must
 * follow the new location (the dentry has to move, not be orphaned). */
FSTEST(rename_dir_updates_cwd)
{
    char *a = pj(dir, "a"), *b = pj(dir, "b");
    ASSERT_OK(mkdir(a, 0755));
    ASSERT_OK(mkdir(pj(a, "sub"), 0755));
    ASSERT_OK(chdir(pj(a, "sub")));
    ASSERT_OK(rename(a, b));

    char cwd[256];
    ASSERT_TRUE(getcwd(cwd, sizeof(cwd)) != NULL);
    EXPECT_STREQ(cwd, pj(b, "sub"));

    struct stat dotdot, bst;
    EXPECT_OK(stat("..", &dotdot));
    EXPECT_OK(stat(b, &bst));
    EXPECT_EQ(dotdot.st_ino, bst.st_ino);

    create_file("x", "1", 1);
    struct stat st;
    EXPECT_OK(stat(pj(pj(b, "sub"), "x"), &st));
    EXPECT_ERR(stat(pj(a, "sub"), &st), ENOENT);
}

FSTEST(rename_back_and_forth)
{
    char *a = pj(dir, "a"), *b = pj(dir, "b");
    create_file(a, "abc", 3);
    for (int i = 0; i < 4; i++) {
        EXPECT_OK(rename(a, b));
        EXPECT_OK(rename(b, a));
    }
    struct stat st;
    EXPECT_ERR(stat(b, &st), ENOENT);
    char buf[8] = {0};
    EXPECT_EQ(slurp(a, buf, sizeof(buf)), 3);
    EXPECT_STREQ(buf, "abc");
}

/* An unlinked file stays usable through an open fd, and closing it must not
 * touch freed memory. */
FSTEST(unlink_open_file_then_close)
{
    char *f = pj(dir, "gone");
    int fd = ASSERT_OK(open(f, O_CREAT | O_RDWR, 0644));
    EXPECT_EQ(write(fd, "hello", 5), 5);
    ASSERT_OK(unlink(f));
    struct stat st;
    EXPECT_ERR(stat(f, &st), ENOENT);
    char buf[8] = {0};
    EXPECT_EQ(pread(fd, buf, 5, 0), 5);
    EXPECT_STREQ(buf, "hello");
    EXPECT_OK(close(fd));
    create_file(f, "new", 3); /* reuse the name */
    EXPECT_EQ(slurp(f, buf, sizeof(buf)), 3);
}

static void churn_names(const char *dir, int n)
{
    char *f = pj(dir, "churn"), *g = pj(dir, "churn2"), *d = pj(dir, "churnd");
    for (int i = 0; i < n; i++) {
        if (create_file(f, NULL, 0) != 0 || rename(f, g) != 0 || unlink(g) != 0 ||
                mkdir(d, 0755) != 0 || rmdir(d) != 0) {
            FAIL("churn #%d failed: errno=%d (%s)", i, errno, strerror(errno));
            ktest_abort();
        }
    }
}

/* unlink/rmdir/rename used to orphan dentries without freeing them */
FSTEST(dentry_no_leak)
{
    enum { N = 500 };
    churn_names(dir, 20);
    long long before = ktest_free_bytes();
    ASSERT_GE(before, 0);
    churn_names(dir, N);
    long long lost = before - ktest_free_bytes();
    ktest_log("%s: free memory lost over %d rounds: %lld KiB", fs->fs, N, lost / 1024);
    EXPECT_LT(lost, 128 * 1024);
}

/* Renaming over an open file: the fd keeps the replaced file's data, and
 * the replaced inode is freed only when that fd closes. */
FSTEST(rename_over_open_target)
{
    char *a = pj(dir, "src"), *b = pj(dir, "dst");
    create_file(a, "new", 3);
    create_file(b, "old!!", 5);
    int fd = ASSERT_OK(open(b, O_RDONLY));
    ASSERT_OK(rename(a, b));
    char buf[8] = {0};
    EXPECT_EQ(pread(fd, buf, 5, 0), 5);
    EXPECT_STREQ(buf, "old!!");
    EXPECT_OK(close(fd));
    memset(buf, 0, sizeof(buf));
    EXPECT_EQ(slurp(b, buf, sizeof(buf)), 3);
    EXPECT_STREQ(buf, "new");
}

static void link_churn(const char *dir, int n)
{
    char *a = pj(dir, "la"), *b = pj(dir, "lb");
    char buf[8];
    for (int i = 0; i < n; i++) {
        if (create_file(a, "abc", 3) != 0 || link(a, b) != 0 || unlink(a) != 0 ||
                slurp(b, buf, sizeof(buf)) != 3 || unlink(b) != 0) {
            FAIL("link churn #%d failed: errno=%d (%s)", i, errno, strerror(errno));
            ktest_abort();
        }
    }
}

/* Each name of a hard-linked file holds the inode; the last one frees it */
FSTEST(hard_link_no_leak)
{
    char *probe = pj(dir, "probe"), *probe2 = pj(dir, "probe2");
    create_file(probe, NULL, 0);
    if (link(probe, probe2) != 0 && errno == EPERM)
        SKIP("%s has no hard links", fs->fs);
    enum { N = 300 };
    link_churn(dir, 20);
    long long before = ktest_free_bytes();
    ASSERT_GE(before, 0);
    link_churn(dir, N);
    long long lost = before - ktest_free_bytes();
    ktest_log("%s: free memory lost over %d rounds: %lld KiB", fs->fs, N, lost / 1024);
    EXPECT_LT(lost, 128 * 1024);
}

/* ================= name length, readdir, big operations ================= */

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* struct dirent holds 127 characters. A longer name must be refused, since
 * readdir would otherwise list a truncated name that open can't find. */
FSTEST(name_length_limit)
{
    char name[201];
    memset(name, 'n', sizeof(name) - 1);
    name[127] = 0;
    char *f = pj(dir, name);
    create_file(f, "x", 1);
    EXPECT_TRUE(dir_contains(dir, name));
    struct stat st;
    EXPECT_OK(stat(f, &st));

    memset(name, 'm', sizeof(name) - 1);
    name[200] = 0;
    int fd = open(pj(dir, name), O_CREAT | O_WRONLY, 0644);
    if (fd >= 0) {
        close(fd);
        FAIL("200-char name was accepted");
    } else {
        EXPECT_EQ(errno, ENAMETOOLONG);
    }
    name[128] = 0;
    EXPECT_ERR(mkdir(pj(dir, name), 0755), ENAMETOOLONG);

    /* everything readdir lists must be reachable by that name */
    DIR *d = opendir(dir);
    ASSERT_TRUE(d != NULL);
    struct dirent *e;
    while ((e = readdir(d)))
        if (stat(pj(dir, e->d_name), &st) != 0)
            FAIL("listed '%.40s...' (%zu chars) not found", e->d_name, strlen(e->d_name));
    closedir(d);
}

/* readdir after rewinddir reflects entries created and removed since opendir */
FSTEST(readdir_after_rewind_sees_changes)
{
    char *sub = pj(dir, "rw");
    ASSERT_OK(mkdir(sub, 0755));
    create_file(pj(sub, "old"), NULL, 0);
    DIR *d = opendir(sub);
    ASSERT_TRUE(d != NULL);
    while (readdir(d))
        ;
    create_file(pj(sub, "new_long_name_entry"), NULL, 0);
    EXPECT_OK(unlink(pj(sub, "old")));
    rewinddir(d);
    bool saw_new = false, saw_old = false;
    struct dirent *e;
    while ((e = readdir(d))) {
        saw_new |= !strcmp(e->d_name, "new_long_name_entry");
        saw_old |= !strcmp(e->d_name, "old");
    }
    closedir(d);
    EXPECT_TRUE(saw_new);
    EXPECT_TRUE(!saw_old);
}

static void list_dir_n(const char *dir, int n)
{
    for (int i = 0; i < n; i++) {
        DIR *d = opendir(dir);
        if (!d) {
            FAIL("opendir #%d: errno=%d", i, errno);
            ktest_abort();
        }
        while (readdir(d))
            ;
        closedir(d);
    }
}

/* opening a directory used to leak a copy of its listing */
FSTEST(dir_open_no_leak)
{
    enum { N = 1000 };
    char *sub = pj(dir, "ls");
    ASSERT_OK(mkdir(sub, 0755));
    for (int i = 0; i < 20; i++) {
        char name[32];
        sprintf(name, "entry_with_long_name_%d", i);
        create_file(pj(sub, name), NULL, 0);
    }
    list_dir_n(sub, 20);
    long long before = ktest_free_bytes();
    ASSERT_GE(before, 0);
    list_dir_n(sub, N);
    long long lost = before - ktest_free_bytes();
    ktest_log("%s: free memory lost over %d listings: %lld KiB", fs->fs, N, lost / 1024);
    EXPECT_LT(lost, 128 * 1024);
}

/* Growing a file by ftruncate should allocate the space in one go, not
 * through a write per 4 KB */
FSTEST(ftruncate_grow_large)
{
    enum { SZ = 16 << 20 };
    char *f = pj(dir, "tbig");
    int fd = ASSERT_OK(open(f, O_CREAT | O_RDWR, 0644));
    EXPECT_EQ(write(fd, "head", 4), 4);
    double t = now_s();
    EXPECT_OK(ftruncate(fd, SZ));
    t = now_s() - t;
    ktest_log("%s: ftruncate to %d MiB took %.2f s", fs->fs, SZ >> 20, t);
    EXPECT_LT((long)(t * 1000), 2000L);
    struct stat st;
    EXPECT_OK(fstat(fd, &st));
    EXPECT_EQ(st.st_size, SZ);
    char buf[8] = {0};
    EXPECT_EQ(pread(fd, buf, 4, 0), 4);
    EXPECT_EQ(memcmp(buf, "head", 4), 0);
    static char chunk[65536];
    for (off_t off = 4; off < SZ; off += 3 << 20) {
        memset(chunk, 1, sizeof(chunk));
        ssize_t n = pread(fd, chunk, sizeof(chunk), off);
        EXPECT_GT(n, 0);
        for (ssize_t i = 0; i < n; i++)
            if (chunk[i]) {
                FAIL("byte %lld = 0x%x", (long long)(off + i), (unsigned char)chunk[i]);
                break;
            }
    }
    close(fd);
    EXPECT_OK(unlink(f));
}

static void create_named(const char *dir, const char *fmt, int from, int to)
{
    char name[64];
    for (int i = from; i < to; i++) {
        sprintf(name, fmt, i);
        if (create_file(pj(dir, name), name, strlen(name)) != 0)
            ktest_abort();
    }
}

static void check_named(const char *dir, const char *fmt, int from, int to)
{
    char name[64], buf[64];
    for (int i = from; i < to; i++) {
        sprintf(name, fmt, i);
        memset(buf, 0, sizeof(buf));
        ssize_t n = slurp(pj(dir, name), buf, sizeof(buf) - 1);
        if (n != (ssize_t)strlen(name) || strcmp(buf, name))
            FAIL("%s: read %zd bytes '%s'", name, n, n > 0 ? buf : "");
    }
}

/* Names sharing a short-name basis each get their own ~N alias, including
 * past ~9 and ~99 where the tail grows, and freed numbers are reused */
FSTEST(many_names_same_prefix)
{
    enum { N = 300 };
    const char *fmt = "shared_prefix_name_%03d.txt", *fmt2 = "shared_prefix_again_%03d.txt";
    char *sub = pj(dir, "alias");
    ASSERT_OK(mkdir(sub, 0755));
    create_named(sub, fmt, 0, N);
    char name[64];
    for (int i = 100; i < 150; i++) {
        sprintf(name, fmt, i);
        EXPECT_OK(unlink(pj(sub, name)));
    }
    create_named(sub, fmt2, 0, 50);

    check_named(sub, fmt, 0, 100);
    check_named(sub, fmt, 150, N);
    check_named(sub, fmt2, 0, 50);
    int count = 0;
    DIR *d = opendir(sub);
    ASSERT_TRUE(d != NULL);
    struct dirent *e;
    while ((e = readdir(d)))
        count += !strncmp(e->d_name, "shared_prefix_", 14);
    closedir(d);
    EXPECT_EQ(count, N);
}

/* A removed directory can live on as a cwd. Creating in it used to reach
 * the filesystem: fat32 then wrote directory entries over the FAT. */
FSTEST(create_in_removed_cwd)
{
    char *d = pj(dir, "gone");
    ASSERT_OK(mkdir(d, 0755));
    ASSERT_OK(chdir(d));
    ASSERT_OK(rmdir(d));
    EXPECT_ERR(open("x", O_CREAT | O_WRONLY, 0644), ENOENT);
    EXPECT_ERR(mkdir("sub", 0755), ENOENT);
    EXPECT_ERR(open("x", O_RDONLY), ENOENT);
    DIR *dp = opendir(".");
    if (dp) {
        errno = 0;
        EXPECT_TRUE(readdir(dp) == NULL);
        closedir(dp);
    }
    ASSERT_OK(chdir("/"));
    /* the parent is intact and still usable */
    char *f = pj(dir, "after");
    int fd = open(f, O_CREAT | O_WRONLY, 0644);
    EXPECT_GE(fd, 0);
    close(fd);
    EXPECT_TRUE(dir_contains(dir, "after"));
    EXPECT_TRUE(!dir_contains(dir, "gone"));
}

FSTEST(ftruncate_past_max_efbig)
{
    char *f = pj(dir, "huge");
    int fd = open(f, O_CREAT | O_RDWR, 0644);
    ASSERT_TRUE(fd >= 0);
    EXPECT_EQ(write(fd, "abc", 3), 3);
    EXPECT_ERR(ftruncate(fd, 5LL << 30), EFBIG);
    struct stat st;
    EXPECT_OK(fstat(fd, &st));
    EXPECT_EQ(st.st_size, 3);
    close(fd);
}

FSTEST(rename_onto_ancestor_enotempty)
{
    char *a = pj(dir, "a"), *b = pj(a, "b");
    ASSERT_OK(mkdir(a, 0755));
    ASSERT_OK(mkdir(b, 0755));
    EXPECT_ERR(rename(b, a), ENOTEMPTY);
    struct stat st;
    EXPECT_OK(stat(b, &st));
}

/* A long name and its 8.3 alias are one file: removing it under either name
 * must remove it under both */
FSTEST(fat_alias_gone_after_unlink)
{
    if (strcmp(fs->fs, "fat32"))
        SKIP("FAT only");
    char *lng = pj(dir, "longfilename.txt"), *sfn = pj(dir, "LONGFI~1.TXT");
    ASSERT_OK(create_file(lng, "x", 1));
    struct stat a, b;
    ASSERT_OK(stat(lng, &a));
    ASSERT_OK(stat(sfn, &b));
    EXPECT_EQ(a.st_ino, b.st_ino);
    ASSERT_OK(unlink(lng));
    EXPECT_ERR(stat(sfn, &b), ENOENT);
    EXPECT_ERR(open(sfn, O_RDONLY), ENOENT);
}

/* A '~' inside the basis must not hide an existing ~N alias */
FSTEST(fat_alias_tilde_in_basis_unique)
{
    if (strcmp(fs->fs, "fat32"))
        SKIP("FAT only");
    ASSERT_OK(create_file(pj(dir, "a~longname1.txt"), "1", 1));
    ASSERT_OK(create_file(pj(dir, "a~longname2.txt"), "2", 1));
    struct stat s1, s2;
    ASSERT_OK(stat(pj(dir, "A~LONG~1.TXT"), &s1));
    ASSERT_OK(stat(pj(dir, "A~LONG~2.TXT"), &s2));
    EXPECT_NE(s1.st_ino, s2.st_ino);
    char buf[4] = {0};
    EXPECT_EQ(slurp(pj(dir, "a~longname2.txt"), buf, sizeof(buf)), 1);
    EXPECT_STREQ(buf, "2");
}
