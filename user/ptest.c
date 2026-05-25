#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

int main(int argc, char *argv[])
{
    int num_procs = atoi(argv[1]);
    for (int i = 0; i < num_procs; i++) {
        int pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        } else if (pid == 0) {
            // Child process
            printf("child process %d\n", getpid());
            return 0;
        } else {
            wait(NULL);
        }
    }
    printf("Successfully created and waited for %d processes\n", num_procs);
    return 0;
}
