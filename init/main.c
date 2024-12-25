#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define SHELL_PROMPT "lilacOS %s # "

int prompt()
{
    char command[256];
    const char cwd[32];
    getcwd(cwd, 32);
    printf(SHELL_PROMPT, cwd);
    char *args[8] = {0};

    memset(command, 0, 256);
    memset(cwd, 0, 32);

    unsigned int r = read(STDIN_FILENO, command, 256);
    command[r-1] = 0; // remove newline

    args[0] = command;
    for (int i = 0, c = 1; i < r && c < 7; i++) {
        if (command[i] == ' ') {
            command[i] = 0;
            args[c++] = &command[i+1];
        }
    }

    pid_t pid = fork();

    if (pid == 0) {
        execv(command, args);
        write(STDERR_FILENO, "Exec failed\n", 12);
        _Exit(EXIT_FAILURE);
    }
    else {
        waitpid(pid, 0, 0);
    }
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
