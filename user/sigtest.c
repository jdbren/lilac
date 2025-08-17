#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void sigint_handler(int signum)
{
    printf("Caught signal number %d (%s)\n", signum, strsignal(signum));
}

int main()
{
    signal(SIGINT, sigint_handler);

    printf("Press Ctrl+C to trigger SIGINT...\n");
    while (1)
        __builtin_ia32_pause();

    return 0;
}
