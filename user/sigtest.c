#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

void sigint_handler(int signum)
{
    printf("Caught signal number %d (%s)\n", signum, strsignal(signum));
}

int main()
{
    char buf[100];
    signal(SIGINT, sigint_handler);

    printf("Entering read syscall...\n");
    read(STDIN_FILENO, buf, 1); // Enter blocking syscall

    return 0;
}
