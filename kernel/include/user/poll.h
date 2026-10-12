#ifndef LILAC_USER_POLL_H
#define LILAC_USER_POLL_H

/* These are specified by iBCS2 */
#define POLLIN      0x0001
#define POLLPRI     0x0002
#define POLLOUT     0x0004
#define POLLERR     0x0008
#define POLLHUP     0x0010
#define POLLNVAL    0x0020

struct pollfd {
    int fd;
    short events;
    short revents;
};

#endif // LILAC_USER_POLL_H
