#ifndef LILAC_SOCKET_H
#define LILAC_SOCKET_H

#define AF_LOCAL    1
#define AF_UNIX     AF_LOCAL
#define AF_INET     2
#define AF_MAX      2

#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define SOCK_RAW        3
#define SOCK_MAX        3

typedef enum {
    SS_FREE,            /* not allocated                */
    SS_UNCONNECTED,     /* unconnected to any socket    */
    SS_CONNECTING,      /* in process of connecting     */
    SS_CONNECTED,       /* connected to socket          */
    SS_DISCONNECTING    /* in process of disconnecting  */
} socket_state;

struct socket;

struct socket_ops {
    int (*bind)(struct socket *sock, char *addr, unsigned addrlen);
};

struct socket {
    socket_state state;
    short domain;
    short type;
    struct file *file;
    const struct socket_ops *sock_ops;
    void *priv;
};

typedef unsigned socklen_t;
#define MAX_SOCKADDR_LEN 128

struct sockaddr {
    unsigned short sa_family;
    char sa_data[];
};

int unix_create_socket(struct socket *sock, int type);

#endif
