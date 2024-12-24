#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    /*
    printf("argc = %d\n", argc);
    printf("argv = %x\n", argv);
    printf("argv[0] at %x = %s\n", argv[0], argv[0]);
    if (argc > 1)
        printf("argv[1] at %x = %s\n", argv[1], argv[1]);
    */
    dirent buf[12];
    char *dir = NULL;

    if (argc == 1)
        dir = "/";
    else
        dir = argv[1];

    int fd = open(dir, 0);
    if (fd < 0)
    {
        printf("ls: cannot access '%s': No such file or directory\n", dir);
        return 1;
    }

    int err = getdents(fd, (void*)buf, sizeof(buf));
    for (int i = 0; i < 12; i++)
    {
        printf("%s\t", buf[i].d_name);
    }
    putchar('\n');
    close(fd);
    return 0;
}
