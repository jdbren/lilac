#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#define SOCK_PATH "/tmp/dgram.sock"

int main()
{
    int sockfd;
    sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("socket");
        return 1;
    }

    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strcpy(addr.sun_path, SOCK_PATH);
    unlink(SOCK_PATH);
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        return 1;
    }

    pid_t pid;
    pid = fork();
    if (pid == -1) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (fd == -1) {
            perror("child socket");
            _exit(1);
        }
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
            perror("connect");
            _exit(1);
        }
        const char *msgs[] = { "hello", "from", "the child" };
        for (int i = 0; i < 3; i++) {
            if (send(fd, msgs[i], strlen(msgs[i]), 0) == -1)
                perror("send");
        }
        close(fd);
        _exit(0);
    }

    char buf[64];
    for (int got = 0; got < 3; ) {
        ssize_t n = recv(sockfd, buf, sizeof(buf) - 1, 0);
        if (n == -1) {
            perror("recv");
            break;
        }
        buf[n] = '\0';
        printf("parent got %zd bytes: \"%s\"\n", n, buf);
        got++;
    }

    waitpid(pid, NULL, 0);
    close(sockfd);
    unlink(SOCK_PATH);
    return 0;
}
