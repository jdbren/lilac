#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/syscall.h>

#define SHELL_PROMPT "lilacOS %s # "

int prompt(void)
{
    char command[256];
    char cwd[32];
    getcwd(cwd, 32);
    printf(SHELL_PROMPT, cwd);
    fflush(stdout);
    char *args[8] = {0};

    memset(command, 0, 256);
    memset(cwd, 0, 32);

    int r = read(STDIN_FILENO, command, 256);
    if (r < 0) {
        printf("Error reading command\n");
        exit(1);
    }
    command[r-1] = 0; // remove newline

    if (strncmp(command, "cd", 2) == 0) {
        char *path = command + 3;
        if (chdir(path) < 0) {
            printf("Error changing directory\n");
            return -1;
        }
        printf("Changed directory to %s\n", path);
        return 0;
    } else if (strncmp(command, "shutdown", 8) == 0) {
        printf("Shutting down...\n");
        syscall1(25, 1);
    }

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
