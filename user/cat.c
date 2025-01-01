#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    int n;
    char buf[512];
    if (argc != 2)
    {
        printf("Usage: %s\n", argv[0], " [message]");
        return 1;
    }

    int fd = open(argv[1], 0);
    if (fd < 0)
    {
        printf("cat: %s: No such file or directory\n", argv[1]);
        return 1;
    }

    while ((n = read(fd, buf, sizeof(buf))) > 0)
    {
        write(STDOUT_FILENO, buf, n);
    }

    close(fd);

    return 0;
}
