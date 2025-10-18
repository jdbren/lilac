#include <stdio.h>
#include <fcntl.h>
#include <sys/dirent.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>

int main(int argc, char *argv[])
{
    struct dirent buf[12] = {0};
    char *dir = NULL;

    if (argc == 1)
        dir = ".";
    else
        dir = argv[1];

    int fd = open(dir, 0, 0);
    if (fd < 0)
    {
        if (errno == ENOENT)
            printf("ls: cannot access '%s': No such file or directory\n", dir);
        return 1;
    }

    int cols = (80 + 1) / (12 + 2);
    int rows = (12 + cols - 1) / cols;

    int err = 0;
    while ((err = getdents(fd, (void*)buf, sizeof(buf))) > 0) {
        if (errno == -ENOTDIR) {
            printf("ls: cannot access '%s': Not a directory\n", dir);
            return 1;
        }
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                int i = c * rows + r;
                if (i < 12) {
                    printf("%-*s", 12 + 2, buf[i].d_name);
                }
            }
            putchar('\n');
        }
    }

    putchar('\n');
    close(fd);
    return 0;
}
