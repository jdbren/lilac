#ifndef LILAC_SOCKET_H
#define LILAC_SOCKET_H

#include <lilac/uaccess.h>

#define AF_LOCAL    1
#define AF_UNIX     AF_LOCAL
#define AF_INET     2
#define AF_MAX      2

#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define SOCK_RAW        3
#define SOCK_MAX        3

#define SOCK_RDM       4
#define SOCK_SEQPACKET 5
#define SOCK_DCCP      6

typedef enum {
    SS_FREE,            /* not allocated                */
    SS_UNCONNECTED,     /* unconnected to any socket    */
    SS_CONNECTING,      /* in process of connecting     */
    SS_CONNECTED,       /* connected to socket          */
    SS_DISCONNECTING    /* in process of disconnecting  */
} socket_state;

enum {
    C_ESTABLISHED = 1,
    C_LISTEN,
    C_CLOSE
};

typedef unsigned short sa_family_t;
typedef unsigned socklen_t;
#define __MAX_SOCKADDR_LEN 127
#define MAX_SOCKADDR_DATA_LEN (__MAX_SOCKADDR_LEN - sizeof(sa_family_t))

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[];
};

typedef struct {
    sa_family_t sa_family;
    char sa_data[MAX_SOCKADDR_DATA_LEN];
    char __null_pad;
} ksockaddr_t;

struct socket;

struct socket_ops {
    int (*bind)(struct socket *sock, ksockaddr_t *addr, socklen_t addrlen);
    int (*connect)(struct socket *sock, ksockaddr_t *addr, socklen_t addrlen);
    int (*listen)(struct socket *sock, int backlog);
    int (*accept)(struct socket *sock, struct socket *newsock);
    ssize_t (*send)(struct socket *sock, const void __user *buf, size_t len, int flags);
    ssize_t (*recv)(struct socket *sock, void __user *buf, size_t len, int flags);

    int (*getname)(struct socket *sock, ksockaddr_t *addr, int peer);
};

struct socket {
    socket_state state;
    spinlock_t lock;
    unsigned char cstate;
    short domain;
    short type;
    struct file *file;
    const struct socket_ops *sock_ops;
    void *priv;
};

int unix_create_socket(struct socket *sock, int type);

static inline int sock_no_listen(struct socket *sock, int backlog)
{
    return -EOPNOTSUPP;
}

static inline int sock_no_accept(struct socket *sock, struct socket *newsock)
{
    return -EOPNOTSUPP;
}

#endif
