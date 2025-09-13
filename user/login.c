#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>

int main()
{
    char name[64] = {0};
    setsid();

    signal(SIGINT, SIG_IGN);

    int fd = open("/dev/tty0", O_RDWR);
    dup(fd);
    dup(fd);

    write(1, "Enter name: ", 12);
    read(0, &name, 63);
    setenv("USER", name, 1);

    // Try to run a shell
    execl("/bin/bash", "bash", NULL); // will not work yet
    execl("/bin/dash", "sh", NULL); // kind of works
    execl("/bin/sh", "sh", NULL);
    execl("/sbin/gush", "gush", NULL); // my custom shell
    return 1;
}
