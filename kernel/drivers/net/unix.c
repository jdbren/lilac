#include <lilac/socket.h>
#include <lilac/fs.h>
#include <lilac/fcntl.h>
#include <lilac/pipe.h>
#include <lilac/wait.h>
#include <lib/hashtable.h>

#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"

DEFINE_HASHTABLE(unix_sockets, 8);
static spinlock_t unix_sockets_lock = SPINLOCK_INIT;

#define UNIX_DGRAM_MAX (1 << 16)
#define UNIX_RCVBUF (1 << 18)

struct unix_msg {
    struct list_head list;
    size_t len;
    ksockaddr_t from;
    char data[];
};

struct unix_sock {
    struct socket *sock;
    struct dentry *bound;        // NULL if unbound
    mutex_t bind_lock;           // serializes bind on this socket
    struct unix_sock *peer;      // for connected sockets

    struct hlist_node hlist;

    spinlock_t rx_lock;
    struct waitqueue rx_wq;     // readers blocked on an empty queue
    struct waitqueue tx_wq;     // writers blocked on a full queue (the peer waits here)
    union {
        struct {                // SOCK_DGRAM
            struct list_head rx_queue;
            size_t rx_bytes;    // queued bytes incl. unix_msg headers, capped at UNIX_RCVBUF
        };
        struct {                // SOCK_STREAM
            struct pipe_buf *rx_buf;
            struct list_head backlog;   // pending connections (listener)
            int max_backlog;
        };
    };
};

static const struct socket_ops unix_stream_ops;
static const struct socket_ops unix_dgram_ops;

int unix_create_socket(struct socket *sock, int type)
{
    const struct socket_ops *ops;
    if (type == SOCK_STREAM)
        ops = &unix_stream_ops;
    else if (type == SOCK_DGRAM)
        ops = &unix_dgram_ops;
    else
        return -EINVAL;

    struct unix_sock *usock = kzmalloc(sizeof(struct unix_sock));
    if (!usock)
        return -ENOMEM;

    usock->sock = sock;
    mutex_init(&usock->bind_lock);
    INIT_HLIST_NODE(&usock->hlist);
    spin_lock_init(&usock->rx_lock);
    waitqueue_init(&usock->rx_wq);
    waitqueue_init(&usock->tx_wq);

    if (sock->type == SOCK_DGRAM) {
        INIT_LIST_HEAD(&usock->rx_queue);
        usock->rx_bytes = 0;
    } else {
        usock->rx_buf = NULL;
        INIT_LIST_HEAD(&usock->backlog);
    }

    sock->priv = usock;
    sock->sock_ops = ops;
    sock->state = SS_UNCONNECTED;
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

int unix_bind_socket(struct socket *sock, ksockaddr_t *addr, unsigned int addrlen)
{
    struct unix_sock *usock = sock->priv;
    struct dentry *d;
    int err = 0;

    if (addr->sa_family != AF_UNIX || addr->sa_data[0] == '\0')
        return -EINVAL;

    mutex_lock(&usock->bind_lock);
    if (usock->bound) {
        err = -EADDRINUSE;
        goto out;
    }

    d = create_socket_dev(addr->sa_data);
    if (IS_ERR(d)) {
        err = PTR_ERR(d);
        goto out;
    }
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
    struct unix_sock *usock = sock->priv, *target_un;

    if (addr->sa_family != AF_UNIX || addr->sa_data[0] == '\0')
        return -EINVAL;

    target_un = unix_find_socket(addr);
    if (IS_ERR(target_un))
        return PTR_ERR(target_un);

    if (target_un->sock->type != SOCK_DGRAM)
        return -EPROTOTYPE;

    usock->peer = target_un;
    sock->state = SS_CONNECTED;

    return 0;
}

ssize_t unix_send_dgram(struct socket *sock, const void __user *buf, size_t len, int flags)
{
    struct unix_sock *usock = sock->priv;
    struct unix_sock *peer = usock->peer;
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
    msg->from = (ksockaddr_t) { .sa_family = AF_UNIX, .sa_data = { 0 } };
    INIT_LIST_HEAD(&msg->list);

    acquire_lock(&peer->rx_lock);
    if (peer->rx_bytes + sizeof(*msg) + len > UNIX_RCVBUF) {
        release_lock(&peer->rx_lock);
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
    struct unix_sock *usock = sock->priv;
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
};



static const struct socket_ops unix_stream_ops = {
    .bind = unix_bind_socket,
};
