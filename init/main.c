#include <errno.h>
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
        }

        pid_t w;
        do {
            w = wait(NULL);
        } while (w != pid && !(w < 0 && errno == ECHILD));
    }

    return 0;
}
