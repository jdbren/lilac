#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>

#define SHELL_PROMPT "[root@lilacOS %s] # "

int ls_main(const char *pwd);

int prompt()
{
    char cwd[32];
    getcwd(cwd, 32);
    printf(SHELL_PROMPT, cwd);
    putchar('\n');
    ls_main(cwd);
}

int ls_main(const char *pwd)
{
    dirent buf[12];
    int fd = open(pwd, 0);
    int err = getdents(fd, (void*)buf, sizeof(buf));
    for (int i = 0; i < 12; i++)
    {
        printf("%s\t", buf[i].d_name);
    }
    putchar('\n');
}

void main(void)
{
    char cmd[256];
    prompt();

}
