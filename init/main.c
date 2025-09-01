#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void)
{
    for (;;) {
        pid_t pid = fork();
        if (pid < 0) return 1; // this will panic

        if (pid == 0) {
            execl("/bin/login", "login", NULL);
            exit(1);
        } else {
            wait(NULL);
        }
    }

    return 0;
}
