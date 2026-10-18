#include <lilac/socket.h>
#include <lilac/fs.h>
#include <lilac/fcntl.h>
#include <lib/hashtable.h>

DEFINE_HASHTABLE(unix_sockets, 8);
static spinlock_t unix_sockets_lock = SPINLOCK_INIT;

struct unix_sock {
    struct socket *sock;
    struct dentry *bound;        // NULL if unbound
    mutex_t bind_lock;           // serializes bind on this socket
    struct unix_sock *peer;      // for connected sockets
    struct hlist_node hlist;
    struct list_head backlog;    // pending connections (listener)
    int max_backlog;
    /* receive buffer + lock + wait queue */
};

static const struct socket_ops unix_stream_socket_ops;
static const struct socket_ops unix_dgram_socket_ops;

int unix_create_socket(struct socket *sock, int type)
{
    const struct socket_ops *ops;
    if (type == SOCK_STREAM)
        ops = &unix_stream_socket_ops;
    else if (type == SOCK_DGRAM)
        ops = &unix_dgram_socket_ops;
    else
        return -EINVAL;

    struct unix_sock *usock = kzmalloc(sizeof(struct unix_sock));
    if (!usock)
        return -ENOMEM;

    usock->sock = sock;
    mutex_init(&usock->bind_lock);
    INIT_HLIST_NODE(&usock->hlist);
    INIT_LIST_HEAD(&usock->backlog);

    sock->priv = usock;
    sock->sock_ops = ops;
    sock->state = SS_UNCONNECTED;
    return 0;
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

static const struct socket_ops unix_stream_socket_ops = {
    .bind = unix_bind_socket,
};

static const struct socket_ops unix_dgram_socket_ops = {
    .bind = unix_bind_socket,
};
