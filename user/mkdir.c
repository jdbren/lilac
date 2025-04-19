#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/errno.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <directory_name>\n", argv[0]);
        return 1;
    }

    const char *dir_name = argv[1];
    int ret = mkdir(dir_name, 0755);
    if (ret == -1) {
        perror("mkdir");
        return 1;
    }

    return 0;
}
