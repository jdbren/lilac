#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>

const char *pwd = "/usr";

#define SHELL_PROMPT "[root@lilacOS %s] # "

int prompt()
{
    char cwd[32];
    getcwd(cwd, 32);
    printf(SHELL_PROMPT, cwd);
}

int ls_main()
{
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
    char cmd[256];
    prompt();

}
