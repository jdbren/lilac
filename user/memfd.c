#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>

static int failures;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (errno %d)\n", msg, errno); failures++; } \
    else printf("ok: %s\n", msg); \
} while (0)

int main(void)
{
    int fd = memfd_create("test", MFD_CLOEXEC);
    CHECK(fd >= 0, "memfd_create");
    if (fd < 0)
        return 1;

    CHECK(fcntl(fd, F_GETFD) & FD_CLOEXEC, "MFD_CLOEXEC sets FD_CLOEXEC");

    int fd2 = memfd_create("test", 0);
    CHECK(fd2 >= 0 && !(fcntl(fd2, F_GETFD) & FD_CLOEXEC),
        "second memfd with same name, no cloexec");
    close(fd2);

    char buf[8192];
    memset(buf, 'A', sizeof(buf));
    CHECK(write(fd, buf, sizeof(buf)) == sizeof(buf), "write 8K");

    char rd[16] = {0};
    lseek(fd, 4096, SEEK_SET);
    CHECK(read(fd, rd, 4) == 4 && memcmp(rd, "AAAA", 4) == 0, "read back");

    char *p = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(p != MAP_FAILED, "mmap MAP_SHARED");
    if (p != MAP_FAILED) {
        CHECK(p[100] == 'A' && p[5000] == 'A', "mapping sees file contents");
        memcpy(p + 10, "hello", 5);
        CHECK(msync(p, 8192, MS_SYNC) == 0, "msync");
        CHECK(pread(fd, rd, 5, 10) == 5 && memcmp(rd, "hello", 5) == 0,
            "write through mapping reaches file after msync");
        munmap(p, 8192);
    }

    errno = 0;
    CHECK(memfd_create("bad", 0x100) == -1 && errno == EINVAL, "bad flags -> EINVAL");

    char longname[300];
    memset(longname, 'x', sizeof(longname) - 1);
    longname[sizeof(longname) - 1] = 0;
    errno = 0;
    CHECK(memfd_create(longname, 0) == -1 && errno == EINVAL, "long name -> EINVAL");

    close(fd);
    printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures != 0;
}
