#ifndef LILAC_USER_POLL_H
#define LILAC_USER_POLL_H

#define POLLIN      0x0001
#define POLLPRI     0x0002
#define POLLOUT     0x0004
#define POLLERR     0x0008
#define POLLHUP     0x0010
#define POLLNVAL    0x0020
#define POLLRDNORM  0x0040
#define POLLRDBAND  0x080
#define POLLWRNORM  0x100
#define POLLWRBAND  0x200
#define POLLMSG     0x400
#define POLLRDHUP   0x2000

struct pollfd {
    int fd;
    short events;
    short revents;
};

#endif // LILAC_USER_POLL_H
