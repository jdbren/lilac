#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file>\n", argv[0]);
        return 1;
    }

    if (unlink(argv[1]) == -1) {
        perror("rm");
        return 1;
    }

    return 0;
}
