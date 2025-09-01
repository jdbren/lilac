#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

int main()
{
    setsid();

    signal(SIGINT, SIG_IGN);

    int fd = open("/dev/tty0", O_RDWR);
    dup(fd);
    dup(fd);

    return execl("/sbin/gush", "gush", NULL);
}
