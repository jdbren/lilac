#pragma GCC diagnostic ignored "-Wanalyzer-malloc-leak"

#include <lilac/socket.h>
#include <lilac/fs.h>
#include <lilac/fcntl.h>
#include <lilac/pipe.h>
#include <lilac/wait.h>
#include <lib/hashtable.h>

#include <asm/barrier.h>

DEFINE_HASHTABLE(unix_sockets, 8);
static spinlock_t unix_sockets_lock = SPINLOCK_INIT;

#define UNIX_DGRAM_MAX (1 << 16)
#define UNIX_RCVBUF (1 << 18)
#define UNIX_STREAM_CHUNK PAGE_SIZE

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
    struct unix_sock *peer;      // ref for connected sockets

    struct hlist_node hlist;

    union {
        struct {                // SOCK_DGRAM
            struct list_head rx_queue;
            size_t rx_bytes;    // queued bytes incl. unix_msg headers, capped at UNIX_RCVBUF
            spinlock_t rx_lock;
        };
        struct {                // SOCK_STREAM
            struct pipe_buf  ring;
            struct waitqueue accept_wq; // acceptors waiting for a connection
            struct waitqueue peer_wq;   // connectors waiting for backlog room
            struct list_head backlog;   // listener: queue head and pending link
            int backlog_count;
            int max_backlog;
        };
    };
};

static inline struct unix_sock * sock_unix(struct socket *sock)
{
    return container_of(sock->si, struct unix_sock, si);
}

static inline void unix_sock_hold(struct unix_sock *u)
{
    sock_hold(&u->si);
}

static inline void unix_sock_put(struct unix_sock *u)
{
    sock_put(&u->si);
}

static void unix_addr_put(struct unix_addr *);
static void unix_sock_free(struct sock_internal *);

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

    init_sock_internal(sock, &usock->si, type);
    usock->si.destruct = unix_sock_free;

    mutex_init(&usock->bind_lock);
    INIT_HLIST_NODE(&usock->hlist);

    if (type == SOCK_DGRAM) {
        INIT_LIST_HEAD(&usock->rx_queue);
        spin_lock_init(&usock->rx_lock);
        usock->rx_bytes = 0;
    } else {
        INIT_LIST_HEAD(&usock->backlog);
        waitqueue_init(&usock->accept_wq);
        waitqueue_init(&usock->peer_wq);
        if (pipe_buf_init(&usock->ring, PAGE_SIZE << 2)) {
            kfree(usock);
            return ERR_PTR(-ENOMEM);
        }
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

static void unix_sock_free(struct sock_internal *si)
{
    struct unix_sock *u = container_of(si, struct unix_sock, si);
    struct unix_msg *msg, *tmp;

    if (si->type == SOCK_DGRAM) {
        list_for_each_entry_safe(msg, tmp, &u->rx_queue, list) {
            list_del(&msg->list);
            unix_addr_put(msg->from);
            kfree(msg);
        }
    } else {
        pipe_buf_free(&u->ring);
    }
    unix_addr_put(u->addr);
    if (u->bound)
        dput(u->bound);
    kfree(u);
}

static struct unix_sock * unix_find_socket(const ksockaddr_t *addr)
{
    struct unix_sock *target_un;
    struct dentry *target = vfs_lookup(addr->sa_data);

    if (IS_ERR(target))
        return ERR_CAST(target);
    if (!target->d_inode) {
        dput(target);
        return ERR_PTR(-ENOENT);
    }
    if (!S_ISSOCK(target->d_inode->i_mode)) {
        dput(target);
        return ERR_PTR(-ECONNREFUSED);
    }

    acquire_lock(&unix_sockets_lock);
    // find the peer socket by its address and establish the connection
    hash_for_each_possible(unix_sockets, target_un, hlist, (unsigned long)target->d_inode) {
        if (target_un->bound && target->d_inode == target_un->bound->d_inode) {
            unix_sock_hold(target_un);
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

static int unix_fill_addr(ksockaddr_t *addr, const struct unix_addr *uaddr)
{
    if (!uaddr) {
        addr->sa_family = AF_UNIX;
        addr->sa_data[0] = '\0';
        return sizeof(sa_family_t);
    }
    memcpy(addr, &uaddr->ksock_addr, sizeof(ksockaddr_t));
    return uaddr->len;
}

static int unix_getname(struct socket *sock, ksockaddr_t *addr, int peer)
{
    struct unix_sock *usock = sock_unix(sock);

    if (peer) {
        if (!usock->peer)
            return -ENOTCONN;
        return unix_fill_addr(addr, usock->peer->addr);
    }
    return unix_fill_addr(addr, usock->addr);
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
        err = -EINVAL;
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
    struct unix_sock *usock = sock_unix(sock), *target_un, *old;

    if (addr->sa_family != AF_UNIX || addr->sa_data[0] == '\0')
        return -EINVAL;

    target_un = unix_find_socket(addr);
    if (IS_ERR(target_un))
        return PTR_ERR(target_un);

    if (target_un->si.type != SOCK_DGRAM) {
        unix_sock_put(target_un);
        return -EPROTOTYPE;
    }

    acquire_lock(&usock->si.lock);
    old = usock->peer;
    usock->peer = target_un;
    sock->state = SS_CONNECTED;
    release_lock(&usock->si.lock);

    if (old)
        unix_sock_put(old);
    return 0;
}

static struct unix_sock * get_sendto_peer(struct unix_sock *usock, ksockaddr_t *addr)
{
    struct unix_sock *peer, *peer_peer;
    if (addr->sa_family != AF_UNIX || addr->sa_data[0] == '\0')
        return ERR_PTR(-EINVAL);
    peer = unix_find_socket(addr);
    if (IS_ERR(peer))
        return peer;

    if (peer->si.type != SOCK_DGRAM)
        return ERR_PTR(-EPROTOTYPE);

    // a connected socket only accepts datagrams from its peer
    acquire_lock(&peer->si.lock);
    peer_peer = peer->peer;
    release_lock(&peer->si.lock);
    if (peer_peer && peer_peer != usock)
        return ERR_PTR(-EPERM);

    return peer;
}

ssize_t unix_sendto_dgram(struct socket *sock, const void __user *buf, size_t len,
    int flags, ksockaddr_t *addr, socklen_t addrlen)
{
    struct unix_sock *usock = sock_unix(sock);
    struct unix_sock *peer;
    struct unix_addr *from = usock->addr;
    struct unix_msg *msg;
    ssize_t ret;

    if (len > UNIX_DGRAM_MAX)
        return -EMSGSIZE;

    if (addr) {
        peer = get_sendto_peer(usock, addr);
        if (IS_ERR(peer))
            return PTR_ERR(peer);
    } else {
        acquire_lock(&usock->si.lock);
        peer = usock->peer;
        if (peer)
            unix_sock_hold(peer);
        release_lock(&usock->si.lock);
        if (!peer)
            return -ENOTCONN;
    }

    // A message that races with the peer closing is freed with it
    ret = -ECONNREFUSED;
    if (peer->si.dead)
        goto out_put;

    ret = -ENOMEM;
    msg = kmalloc(sizeof(*msg) + len);
    if (!msg)
        goto out_put;
    if (sock_copy_from(msg->data, buf, len, flags)) {
        kfree(msg);
        ret = -EFAULT;
        goto out_put;
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
        ret = -EAGAIN;
        goto out_put;
    }
    list_add_tail(&msg->list, &peer->rx_queue);
    peer->rx_bytes += sizeof(*msg) + len;
    release_lock(&peer->rx_lock);
    wake_all(&peer->si.read_wq);
    ret = len;

out_put:
    unix_sock_put(peer);
    return ret;
}

ssize_t unix_recvfrom_dgram(struct socket *sock, void __user *buf, size_t len,
    int flags, ksockaddr_t *srcaddr, socklen_t *addrlen)
{
    struct unix_sock *usock = sock_unix(sock);
    struct unix_msg *msg;
    ssize_t ret = 0;

    acquire_lock(&usock->rx_lock);
    while (list_empty(&usock->rx_queue)) {
        release_lock(&usock->rx_lock);
        int err = wait_event_interruptible(usock->si.read_wq, !list_empty(&usock->rx_queue));
        if (err)
            return err;
        acquire_lock(&usock->rx_lock);
    }

    msg = list_first_entry(&usock->rx_queue, struct unix_msg, list);
    list_del(&msg->list);
    usock->rx_bytes -= sizeof(*msg) + msg->len;
    release_lock(&usock->rx_lock);

    size_t n = len < msg->len ? len : msg->len;
    if (sock_copy_to(buf, msg->data, n, flags)) {
        ret = -EFAULT;
        goto out;
    }
    ret = n;
    if (srcaddr)
        *addrlen = unix_fill_addr(srcaddr, msg->from);
out:
    unix_addr_put(msg->from);
    kfree(msg);
    return ret;
}

static void unix_release_sock(struct unix_sock *u)
{
    struct sock_internal *si = &u->si;
    struct unix_sock *peer, *embryo, *tmp;
    LIST_HEAD(pending);

    // no new lookups
    acquire_lock(&unix_sockets_lock);
    hash_del(&u->hlist);
    release_lock(&unix_sockets_lock);

    acquire_lock(&si->lock);
    si->dead = 1;
    si->shutdown = 1;
    si->sock = NULL;
    peer = u->peer;
    u->peer = NULL;
    if (u->si.type == SOCK_STREAM && si->cstate == C_LISTEN) {
        list_splice_init(&u->backlog, &pending);
        u->backlog_count = 0;
    }
    WRITE_ONCE(si->cstate, C_CLOSE);
    release_lock(&si->lock);

    if (u->si.type == SOCK_STREAM) {
        wake_all(&u->peer_wq);  // blocked connectors see C_CLOSE and fail
        wake_all(&si->write_wq);
        if (peer) {
            acquire_lock(&peer->si.lock);
            peer->si.shutdown = 1;
            release_lock(&peer->si.lock);
            wake_all(&peer->si.read_wq);
        }
    }
    if (peer)
        unix_sock_put(peer);

    // never-accepted connections
    list_for_each_entry_safe(embryo, tmp, &pending, backlog) {
        list_del_init(&embryo->backlog);
        unix_release_sock(embryo);
    }

    unix_sock_put(u);
}

static int unix_socketpair(struct socket *sock1, struct socket *sock2)
{
    struct unix_sock *u1 = sock_unix(sock1), *u2 = sock_unix(sock2);

    unix_sock_hold(u1);
    unix_sock_hold(u2);
    u1->peer = u2;
    u2->peer = u1;

    if (sock1->type == SOCK_STREAM) {
        u1->si.cstate = C_ESTABLISHED;
        u2->si.cstate = C_ESTABLISHED;
    }
    sock1->state = SS_CONNECTED;
    sock2->state = SS_CONNECTED;
    return 0;
}

static int unix_release(struct socket *sock)
{
    unix_release_sock(sock_unix(sock));
    sock->si = NULL;
    return 0;
}

static const struct socket_ops unix_dgram_ops = {
    .bind = unix_bind_socket,
    .connect = unix_connect_dgram,
    .listen = sock_no_listen,
    .accept = sock_no_accept,
    .socketpair = unix_socketpair,
    .sendto = unix_sendto_dgram,
    .recvfrom = unix_recvfrom_dgram,
    .release = unix_release,
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

static int unix_accept(struct socket *sock, struct socket *newsock)
{
    struct unix_sock *u = sock_unix(sock), *peer = NULL;
    struct sock_internal *si = &u->si;
    int err = -EOPNOTSUPP;

    if (sock->type != SOCK_STREAM && sock->type != SOCK_SEQPACKET)
        goto out;

    err = -EINVAL;
    if (READ_ONCE(si->cstate) != C_LISTEN)
        goto out;

    acquire_lock(&si->lock);
    while (list_empty(&u->backlog)) {
        release_lock(&si->lock);
        err = wait_event_interruptible(u->accept_wq, !list_empty(&u->backlog));
        if (err)
            goto out;
        acquire_lock(&si->lock);
    }

    peer = list_first_entry(&u->backlog, struct unix_sock, backlog);
    list_del_init(&peer->backlog);
    u->backlog_count--;
    release_lock(&si->lock);

    wake_all(&u->peer_wq);

    newsock->si = &peer->si;
    peer->si.sock = newsock;
    newsock->state = SS_CONNECTED;
    return 0;
out:
    return err;
}

static int unix_connect_stream(struct socket *sock, ksockaddr_t *addr, socklen_t addrlen)
{
    struct sock_internal *si = sock->si, *newsi = NULL, *other = NULL;
    struct unix_sock *u = sock_unix(sock), *newu, *otheru;
    int err;

    if (addr->sa_family != AF_UNIX || addr->sa_data[0] == '\0')
        return -EINVAL;

    // create new sock for complete connection
    newu = __unix_create_sock(NULL, sock->type);
    if (IS_ERR(newu)) {
        err = PTR_ERR(newu);
        goto out;
    }
    newsi = &newu->si;

restart:
    // Find listening sock
    otheru = unix_find_socket(addr);
    if (IS_ERR(otheru)) {
        err = PTR_ERR(otheru);
        goto out_free;
    }

    other = &otheru->si;
    acquire_lock(&other->lock);

    // closed after the lookup found it
    if (other->dead) {
        release_lock(&other->lock);
        unix_sock_put(otheru);
        goto restart;
    }

    if (other == si) {
        err = -EINVAL;
        goto out_unlock;
    }

    if (otheru->si.type != sock->type) {
        err = -EPROTOTYPE;
        goto out_unlock;
    }

    if (other->cstate != C_LISTEN || other->shutdown) {
        err = -ECONNREFUSED;
        goto out_unlock;
    }

    if (READ_ONCE(otheru->backlog_count) > otheru->max_backlog) {
        release_lock(&other->lock);
        err = wait_event_interruptible(otheru->peer_wq,
            READ_ONCE(otheru->backlog_count) <= READ_ONCE(otheru->max_backlog) ||
            READ_ONCE(other->cstate) != C_LISTEN);
        unix_sock_put(otheru);
        if (err)
            goto out_free;
        goto restart;
    }

    acquire_lock(&si->lock);

    if (unlikely(si->cstate != C_CLOSE)) {
        err = si->cstate == C_ESTABLISHED ? -EISCONN : -EINVAL;
        release_lock(&si->lock);
        goto out_unlock;
    }

    unix_sock_hold(u);
    newu->peer = u;
    newsi->cstate = C_ESTABLISHED;

    if (otheru->bound) {
        dget(otheru->bound);
        newu->bound = otheru->bound;
    }
    otheru->addr->refcount++;
    smp_store_release(&newu->addr, otheru->addr);

    WRITE_ONCE(si->cstate, C_ESTABLISHED);

    unix_sock_hold(newu);
    u->peer = newu;
    sock->state = SS_CONNECTED;

    release_lock(&si->lock);

    list_add_tail(&newu->backlog, &otheru->backlog);
    otheru->backlog_count++;
    release_lock(&other->lock);

    wake_all(&otheru->accept_wq);

    unix_sock_put(otheru);
    return 0;

out_unlock:
    release_lock(&other->lock);
    unix_sock_put(otheru);
out_free:
    unix_sock_put(newu);
out:
    return err;
}

ssize_t unix_stream_send(struct socket *sock, const void __user *buf, size_t len, int flags)
{
    struct unix_sock *usock = sock_unix(sock), *peer;
    ssize_t sent = 0;
    size_t chunk = 0, off = 0;
    char *kbuf;
    int err = 0;

    acquire_lock(&usock->si.lock);
    peer = usock->peer;
    if (!peer) {
        release_lock(&usock->si.lock);
        return -ENOTCONN;
    }
    unix_sock_hold(peer);
    release_lock(&usock->si.lock);

    kbuf = kmalloc(UNIX_STREAM_CHUNK);
    if (!kbuf) {
        unix_sock_put(peer);
        return -ENOMEM;
    }

    while (sent < len) {
        if (off == chunk) {
            chunk = MIN(len - sent, UNIX_STREAM_CHUNK);
            off = 0;
            if (sock_copy_from(kbuf, (const char __user *)buf + sent, chunk, flags)) {
                err = -EFAULT;
                break;
            }
        }

        if (peer->si.dead || usock->si.shutdown) {
            err = -EPIPE;
            do_raise(current, SIGPIPE);
            break;
        }

        acquire_lock(&peer->ring.data_lock);
        if (pipe_buf_full(&peer->ring)) {
            release_lock(&peer->ring.data_lock);
            if ((err = wait_event_interruptible(peer->si.write_wq,
                    !pipe_buf_full(&peer->ring) || peer->si.dead)))
                break;
        } else {
            unsigned int n = pipe_buf_in(&peer->ring, kbuf + off, chunk - off);
            release_lock(&peer->ring.data_lock);
            off += n;
            sent += n;
            wake_all(&peer->si.read_wq);
        }
    }

    kfree(kbuf);
    unix_sock_put(peer);
    return sent ? sent : err;
}

ssize_t unix_stream_recv(struct socket *sock, void __user *buf, size_t len, int flags)
{
    struct unix_sock *usock = sock_unix(sock);
    ssize_t recvd = 0;
    char *kbuf;
    int err = 0;

    if (usock->si.cstate != C_ESTABLISHED)
        return -ENOTCONN;
    if (len == 0)
        return 0;

    len = MIN(len, UNIX_STREAM_CHUNK);
    kbuf = kmalloc(len);
    if (!kbuf)
        return -ENOMEM;

    for (;;) {
        acquire_lock(&usock->ring.data_lock);
        if (pipe_buf_empty(&usock->ring)) {
            if (usock->si.shutdown) {
                release_lock(&usock->ring.data_lock);
                break;
            }
            release_lock(&usock->ring.data_lock);
            err = wait_event_interruptible(usock->si.read_wq,
                !pipe_buf_empty(&usock->ring) || usock->si.shutdown);
            if (err) break;
        } else {
            unsigned int n = pipe_buf_out(&usock->ring, kbuf, len);
            release_lock(&usock->ring.data_lock);
            wake_all(&usock->si.write_wq);
            if (sock_copy_to(buf, kbuf, n, flags))
                err = -EFAULT;
            else
                recvd = n;
            break;
        }
    }

    kfree(kbuf);
    return recvd ? recvd : err;
}

ssize_t unix_stream_sendto(struct socket *sock, const void __user *buf, size_t len,
        int flags, ksockaddr_t *addr, socklen_t addrlen)
{
    return unix_stream_send(sock, buf, len, flags);
}

ssize_t unix_stream_recvfrom(struct socket *sock, void __user *buf, size_t len,
        int flags, ksockaddr_t *srcaddr, socklen_t *addrlen)
{
    return unix_stream_recv(sock, buf, len, flags);
}

static const struct socket_ops unix_stream_ops = {
    .bind = unix_bind_socket,
    .listen = unix_listen,
    .accept = unix_accept,
    .connect = unix_connect_stream,
    .socketpair = unix_socketpair,
    .sendto = unix_stream_sendto,
    .recvfrom = unix_stream_recvfrom,
    .release = unix_release,
    .getname = unix_getname,
};
