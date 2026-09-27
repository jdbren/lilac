// Exec target for the test suites. Not a test itself.
//   helper exit N        exit with status N
//   helper argc ...      exit with argc
//   helper argv ...      print argv[2..] separated by spaces
//   helper env NAME      print $NAME (or "(unset)")
//   helper fdopen N      exit 0 if fd N is open, 1 otherwise
//   helper pid           print "pid ppid"
//   helper cat           copy stdin to stdout
//   helper sleep N       sleep N seconds, exit 0
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 2)
        return 100;
    const char *cmd = argv[1];

    if (!strcmp(cmd, "exit") && argc > 2)
        return atoi(argv[2]);
    if (!strcmp(cmd, "argc"))
        return argc;
    if (!strcmp(cmd, "argv")) {
        for (int i = 2; i < argc; i++)
            printf("%s%s", argv[i], i + 1 < argc ? " " : "");
        printf("\n");
        return 0;
    }
    if (!strcmp(cmd, "env") && argc > 2) {
        const char *v = getenv(argv[2]);
        printf("%s\n", v ? v : "(unset)");
        return 0;
    }
    if (!strcmp(cmd, "fdopen") && argc > 2)
        return fcntl(atoi(argv[2]), F_GETFD) == -1 ? 1 : 0;
    if (!strcmp(cmd, "pid")) {
        printf("%d %d\n", (int)getpid(), (int)getppid());
        return 0;
    }
    if (!strcmp(cmd, "cat")) {
        char buf[4096];
        ssize_t n;
        while ((n = read(0, buf, sizeof(buf))) > 0)
            if (write(1, buf, n) != n)
                return 1;
        return n < 0;
    }
    if (!strcmp(cmd, "sleep") && argc > 2) {
        sleep(atoi(argv[2]));
        return 0;
    }
    return 101;
}
