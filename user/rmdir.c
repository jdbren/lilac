#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <directory>\n", argv[0]);
        return 1;
    }

    if (rmdir(argv[1]) == -1) {
        perror("rmdir");
        return 1;
    }

    return 0;
}
