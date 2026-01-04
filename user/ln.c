#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int opt;
    int symbolic = 0;

    while ((opt = getopt(argc, argv, "s")) != -1) {
        switch (opt) {
        case 's':
            symbolic = 1;
            break;
        default:
            fprintf(stderr, "Usage: %s [-s] <target> <link>\n", argv[0]);
            return 1;
        }
    }

    if (argc - optind < 2) {
        fprintf(stderr, "Usage: %s [-s] <target> <link>\n", argv[0]);
        return 1;
    }

    const char *target = argv[optind];
    const char *linkpath = argv[optind + 1];

    int ret;
    if (symbolic) {
        ret = symlink(target, linkpath);
    } else {
        ret = link(target, linkpath);
    }

    if (ret < -1) {
        perror("ln");
        return 1;
    }

    return 0;
}
