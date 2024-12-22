#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <sys/wait.h>

#define SHELL_PROMPT "lilacOS %s # "

int ls_main(const char *pwd);

int prompt()
{
    char command[256];
    const char cwd[32];
    getcwd(cwd, 32);
    printf(SHELL_PROMPT, cwd);

    unsigned int r = read(0, command, 256);
    command[r-1] = 0; // remove newline

    pid_t pid = fork();

    if (pid == 0) {
        printf("child %d\n", pid);
        _exit(0);
    }
    else {
        printf("parent: pid %d\n", pid);
        waitpid(pid, 0, 0);
    }
    return 0;
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
    close(fd);
    return 0;
}

int main(void)
{
    open("/dev/console", 0);
    open("/dev/console", 0);
    open("/dev/console", 0);
    while (1)
        prompt();

    return 0;
}
