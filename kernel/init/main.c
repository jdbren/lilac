#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>

const char *pwd = "/usr";

int ls_main()
{
    printf("Listing directory %s\n", pwd);
    dirent buf[12];
    int fd = open(pwd, 0);
    printf("fd: %d\n", fd);
    getdents(fd, (void*)buf, sizeof(buf));
    for (int i = 0; i < 12; i++)
    {
        printf("%s\n", buf[i].d_name);
    }
}

void main(void)
{
    printf("Starting init task\n");

    ls_main();
}
