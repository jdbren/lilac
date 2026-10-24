#include <lilac/socket.h>
#include <lilac/fs.h>
#include <lilac/fcntl.h>
#include <lilac/pipe.h>
#include <lilac/wait.h>
#include <lib/hashtable.h>

#include <asm/barrier.h>

#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"

DEFINE_HASHTABLE(unix_sockets, 8);
static spinlock_t unix_sockets_lock = SPINLOCK_INIT;

#define UNIX_DGRAM_MAX (1 << 16)
#define UNIX_RCVBUF (1 << 18)

struct unix_addr {
    atomic_uint refcount;
    int len;
    ksockaddr_t ksock_addr;
};

struct unix_msg {
    struct list_head list;
    size_t len;
    struct unix_addr *from;
    char data[];
};

struct unix_sock {
    struct sock_internal si;     // si.lock: cstate, peer
    struct unix_addr *addr;      // address this socket is bound to
    struct dentry *bound;        // NULL if unbound
    mutex_t bind_lock;           // bind sleeps in vfs_mknod
    struct unix_sock *peer;      // for connected sockets

    struct hlist_node hlist;

    union {
        struct {                // SOCK_DGRAM
            struct list_head rx_queue;
            size_t rx_bytes;    // queued bytes incl. unix_msg headers, capped at UNIX_RCVBUF
            struct waitqueue rx_wq;     // readers blocked on an empty queue
            spinlock_t rx_lock;
        };
        struct {                // SOCK_STREAM
            struct pipe_buf *rx_buf;
            struct waitqueue accept_wq; // acceptors waiting for a connection
            struct waitqueue peer_wq;   // connectors waiting for backlog room
            struct list_head backlog;   // listener: queue head and pending link
            int backlog_count;
            int max_backlog;
        };
    };
};

static struct unix_sock * sock_unix(struct socket *sock)
{
    return container_of(sock->si, struct unix_sock, si);
}

static const struct socket_ops unix_stream_ops;
static const struct socket_ops unix_dgram_ops;

static struct unix_sock * __unix_create_sock(struct socket *sock, int type)
{
    const struct socket_ops *ops;
    if (type == SOCK_STREAM)
        ops = &unix_stream_ops;
    else if (type == SOCK_DGRAM)
        ops = &unix_dgram_ops;
    else
        return ERR_PTR(-EINVAL);

    struct unix_sock *usock = kzmalloc(sizeof(struct unix_sock));
    if (!usock)
        return ERR_PTR(-ENOMEM);

    init_sock_internal(sock, &usock->si);

    mutex_init(&usock->bind_lock);
    INIT_HLIST_NODE(&usock->hlist);

    if (type == SOCK_DGRAM) {
        INIT_LIST_HEAD(&usock->rx_queue);
        spin_lock_init(&usock->rx_lock);
        waitqueue_init(&usock->rx_wq);
        usock->rx_bytes = 0;
    } else {
        usock->rx_buf = NULL;
        INIT_LIST_HEAD(&usock->backlog);
        waitqueue_init(&usock->accept_wq);
        waitqueue_init(&usock->peer_wq);
    }

    if (sock) {
        sock->sock_ops = ops;
        sock->state = SS_UNCONNECTED;
    }
    return usock;
}

int unix_create_socket(struct socket *sock, int type)
{
    struct unix_sock *usock = __unix_create_sock(sock, type);
    if (IS_ERR(usock))
        return PTR_ERR(usock);
    return 0;
}

static struct unix_sock * unix_find_socket(const ksockaddr_t *addr)
{
    struct unix_sock *target_un;
    struct dentry *target = vfs_lookup(addr->sa_data);

    if (IS_ERR(target))
        return ERR_CAST(target);
    if (!target->d_inode || !S_ISSOCK(target->d_inode->i_mode)) {
        dput(target);
        return ERR_PTR(-ECONNREFUSED);
    }

    acquire_lock(&unix_sockets_lock);
    // find the peer socket by its address and establish the connection
    hash_for_each_possible(unix_sockets, target_un, hlist, (unsigned long)target->d_inode) {
        if (target_un->bound && target->d_inode == target_un->bound->d_inode) {
            release_lock(&unix_sockets_lock);
            dput(target);
            return target_un;
        }
    }

    release_lock(&unix_sockets_lock);
    dput(target);
    return ERR_PTR(-ECONNREFUSED);
}

static struct dentry * create_socket_dev(const char *addr)
{
    int err = vfs_mknod(addr, S_IFSOCK|S_IRUSR|S_IWUSR, 0);
    if (err) {
        if (err == -EEXIST) err = -EADDRINUSE;
        return ERR_PTR(err);
    }

    struct dentry *d = vfs_lookup(addr);
    if (IS_ERR(d))
        return d;
    if (!d->d_inode || !S_ISSOCK(d->d_inode->i_mode)) {
        dput(d);
        return ERR_PTR(-ENOENT);
    }

    return d;
}

static struct unix_addr * build_unix_addr(const ksockaddr_t *addr)
{
    struct unix_addr *uaddr = kzmalloc(sizeof(struct unix_addr));
    if (!uaddr)
        return NULL;
    uaddr->refcount = 1;
    uaddr->len = offsetof(ksockaddr_t, sa_data) + strlen(addr->sa_data) + 1;
    memcpy(&uaddr->ksock_addr, addr, sizeof(ksockaddr_t));
    return uaddr;
}

static void unix_addr_put(struct unix_addr *uaddr)
{
    if (uaddr && --uaddr->refcount == 0)
        kfree(uaddr);
}

static int unix_getname(struct socket *sock, ksockaddr_t *addr, int peer)
{
    struct unix_sock *usock = sock_unix(sock);
    struct unix_addr *uaddr;

    if (peer) {
        if (!usock->peer)
            return -ENOTCONN;
        uaddr = usock->peer->addr;
    } else {
        uaddr = usock->addr;
    }

    if (!uaddr) {
        addr->sa_family = AF_UNIX;
        addr->sa_data[0] = '\0';
        return sizeof(sa_family_t);
    }
    memcpy(addr, &uaddr->ksock_addr, sizeof(ksockaddr_t));
    return uaddr->len;
}

int unix_bind_socket(struct socket *sock, ksockaddr_t *addr, unsigned int addrlen)
{
    struct unix_sock *usock = sock_unix(sock);
    struct unix_addr *uaddr;
    struct dentry *d;
    int err = 0;

    if (addr->sa_family != AF_UNIX || addr->sa_data[0] == '\0')
        return -EINVAL;

    mutex_lock(&usock->bind_lock);
    if (usock->bound) {
        err = -EADDRINUSE;
        goto out;
    }

    uaddr = build_unix_addr(addr);
    if (!uaddr) {
        err = -ENOMEM;
        goto out;
    }

    d = create_socket_dev(addr->sa_data);
    if (IS_ERR(d)) {
        unix_addr_put(uaddr);
        err = PTR_ERR(d);
        goto out;
    }
    usock->addr = uaddr;
    usock->bound = d;

    acquire_lock(&unix_sockets_lock);
    hash_add(unix_sockets, &usock->hlist, (unsigned long)d->d_inode);
    release_lock(&unix_sockets_lock);
out:
    mutex_unlock(&usock->bind_lock);
    return err;
}

//
// DATAGRAM
//

int unix_connect_dgram(struct socket *sock, ksockaddr_t *addr, unsigned int addrlen)
{
    struct unix_sock *usock = sock_unix(sock), *target_un;

    if (addr->sa_family != AF_UNIX || addr->sa_data[0] == '\0')
        return -EINVAL;

    target_un = unix_find_socket(addr);
    if (IS_ERR(target_un))
        return PTR_ERR(target_un);

    if (target_un->si.sock->type != SOCK_DGRAM)
        return -EPROTOTYPE;

    usock->peer = target_un;
    sock->state = SS_CONNECTED;

    return 0;
}

ssize_t unix_send_dgram(struct socket *sock, const void __user *buf, size_t len, int flags)
{
    struct unix_sock *usock = sock_unix(sock);
    struct unix_sock *peer = usock->peer;
    struct unix_addr *from = usock->addr;
    struct unix_msg *msg;

    if (!peer)
        return -ENOTCONN;
    if (len > UNIX_DGRAM_MAX)
        return -EMSGSIZE;

    msg = kmalloc(sizeof(*msg) + len);
    if (!msg)
        return -ENOMEM;
    if (copy_from_user(msg->data, buf, len)) {
        kfree(msg);
        return -EFAULT;
    }
    msg->len = len;
    if (from)
        from->refcount++;
    msg->from = from;
    INIT_LIST_HEAD(&msg->list);

    acquire_lock(&peer->rx_lock);
    if (peer->rx_bytes + sizeof(*msg) + len > UNIX_RCVBUF) {
        release_lock(&peer->rx_lock);
        unix_addr_put(msg->from);
        kfree(msg);
        return -EAGAIN;
    }
    list_add_tail(&msg->list, &peer->rx_queue);
    peer->rx_bytes += sizeof(*msg) + len;
    release_lock(&peer->rx_lock);
    wake_all(&peer->rx_wq);

    return len;
}

ssize_t unix_recv_dgram(struct socket *sock, void __user* buf, size_t len, int flags)
{
    struct unix_sock *usock = sock_unix(sock);
    struct unix_msg *msg;
    ssize_t ret = 0;

    acquire_lock(&usock->rx_lock);
    if (list_empty(&usock->rx_queue)) {
        release_lock(&usock->rx_lock);
        int err = wait_event_interruptible(usock->rx_wq, !list_empty(&usock->rx_queue));
        if (err)
            return err;
    } else {
        release_lock(&usock->rx_lock);
    }

    msg = list_first_entry(&usock->rx_queue, struct unix_msg, list);
    list_del(&msg->list);
    usock->rx_bytes -= sizeof(*msg) + msg->len;
    release_lock(&usock->rx_lock);

    size_t n = len < msg->len ? len : msg->len;
    if (copy_to_user(buf, msg->data, n)) {
        ret = -EFAULT;
        goto out;
    }
    ret = n;
out:
    unix_addr_put(msg->from);
    kfree(msg);
    return ret;
}

static const struct socket_ops unix_dgram_ops = {
    .bind = unix_bind_socket,
    .connect = unix_connect_dgram,
    .listen = sock_no_listen,
    .accept = sock_no_accept,
    .send = unix_send_dgram,
    .recv = unix_recv_dgram,
    .getname = unix_getname,
};

static int unix_listen(struct socket *sock, int backlog)
{
    int err;
    struct unix_sock *u = sock_unix(sock);
    struct sock_internal *si = &u->si;

    err = -EOPNOTSUPP;
    if (sock->type != SOCK_STREAM && sock->type != SOCK_SEQPACKET)
        goto out;	/* Only stream/seqpacket sockets accept */
    err = -EINVAL;
    if (!READ_ONCE(u->addr))
        goto out;	/* No listens on an unbound socket */

    acquire_lock(&si->lock);
    err = -EINVAL;
    if (si->cstate != C_CLOSE && si->cstate != C_LISTEN)
        goto out_unlock;
    if (backlog > u->max_backlog)
        wake_all(&u->peer_wq);
    u->max_backlog = backlog;
    WRITE_ONCE(si->cstate, C_LISTEN);

    err = 0;

out_unlock:
    release_lock(&si->lock);
out:
    return err;
}
out:
    return err;
}

static const struct socket_ops unix_stream_ops = {
    .bind = unix_bind_socket,
    .listen = unix_listen,
    .getname = unix_getname,
};
