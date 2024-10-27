#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>

#define SHELL_PROMPT "lilacOS %s # "

int ls_main(const char *pwd);

int prompt()
{
    char dirname[128];
    const char cwd[32];
    getcwd(cwd, 32);
    printf(SHELL_PROMPT, cwd);

    unsigned int r = read(0, dirname, 128);
    dirname[r-1] = 0; // remove newline
    ls_main(dirname);
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

int main(void)
{
    open("/dev/console", 0);
    open("/dev/console", 0);
    open("/dev/console", 0);
    prompt();

    return 0;
}
