#include <lilac/socket.h>
#include <lilac/fdtable.h>
#include <lilac/fs.h>
#include <lilac/syscall.h>
#include <lilac/uaccess.h>
#include <mm/kmalloc.h>

static struct file * get_socket_file(int sockfd)
{
    struct file *file = fget_handle(sockfd);
    if (IS_ERR(file))
        return file;
    if (!file->f_inode || !S_ISSOCK(file->f_inode->i_mode)) {
        fput(file);
        return ERR_PTR(-ENOTSOCK);
    }
    return file;
}

static struct socket * alloc_socket(int domain, int type)
{
    struct socket *sock = kzmalloc(sizeof(struct socket));
    if (!sock)
        return NULL;
    sock->domain = domain;
    sock->type = type;
    sock->state = SS_FREE;
    spin_lock_init(&sock->lock);
    return sock;
}

static int init_socket(struct socket *sock, int domain, int type)
{
    switch (domain) {
    case AF_UNIX:
        return unix_create_socket(sock, type);
    default:
        return -EINVAL;
    }
}

static void sock_destruct(struct sock_internal *si)
{
    kfree(si);
}

void init_sock_internal(struct socket *sock, struct sock_internal *si, int type)
{
    if (sock)
        sock->si = si;
    si->sock = sock;
    si->refcount = 1;
    spin_lock_init(&si->lock);
    si->cstate = C_CLOSE;
    si->type = type;
    si->destruct = sock_destruct;
}

static int socket_release(struct inode *inode, struct file *file)
{
    struct socket *sock = file->f_data;

    if (sock->si)
        sock->sock_ops->release(sock);
    kfree(sock);
    return 0;
}

static const struct file_operations socket_fops = {
    .release = socket_release,
};

static struct file * alloc_socket_file(struct socket *sock)
{
    struct inode *inode = new_anon_inode(S_IFSOCK | S_IREAD | S_IWRITE, &socket_fops);
    if (!inode)
        return ERR_PTR(-ENOMEM);

    struct file *file = alloc_anon_file(inode, O_RDWR, sock);
    if (!file) {
        iput(inode);
        return ERR_PTR(-ENOMEM);
    }

    return file;
}

SYSCALL_DECL3(socket, int, domain, int, type, int, protocol)
{
    int fd;
    int err = 0;
    struct socket *sock;
    struct file *file;

    if ((unsigned)domain > AF_MAX || (unsigned)type > SOCK_MAX)
        return -EINVAL;

    fd = get_unused_fd();
    if (fd < 0)
        return -EMFILE;

    sock = alloc_socket(domain, type);
    if (!sock) {
        put_unused_fd(fd);
        return -ENOMEM;
    }

    file = alloc_socket_file(sock);
    if (IS_ERR(file)) {
        err = PTR_ERR(file);
        kfree(sock);
        goto err_fd;
    }

    err = init_socket(sock, domain, type);
    if (err) {
        fput(file);
        goto err_fd;
    }

    sock->file = file;
    fd_install(fd, file);
    return fd;

err_fd:
    put_unused_fd(fd); // also frees sock
    return err;
}

static int get_user_sockaddr(ksockaddr_t *ksockaddr, const struct sockaddr *addr,
                             socklen_t addrlen)
{
    if (addrlen < sizeof(struct sockaddr) + 1 || addrlen > __MAX_SOCKADDR_LEN)
        return -EINVAL;

    if (copy_from_user(ksockaddr, addr, addrlen))
        return -EFAULT;

    ksockaddr->sa_data[addrlen - sizeof(sa_family_t)] = '\0';
    return 0;
}

SYSCALL_DECL3(bind, int, sockfd, const struct sockaddr *, addr, socklen_t, addrlen)
{
    struct file *file;
    struct socket *sock;
    ksockaddr_t ksockaddr;
    int err;

    file = get_socket_file(sockfd);
    if (IS_ERR(file))
        return PTR_ERR(file);
    sock = file->f_data;

    if ((err = get_user_sockaddr(&ksockaddr, addr, addrlen)) < 0)
        goto out;

    err = sock->sock_ops->bind(sock, &ksockaddr, addrlen);
out:
    fput(file);
    return err;
}

SYSCALL_DECL3(connect, int, sockfd, const struct sockaddr *, addr, socklen_t, addrlen)
{
    struct file *file;
    struct socket *sock;
    ksockaddr_t ksockaddr;
    int err;

    file = get_socket_file(sockfd);
    if (IS_ERR(file))
        return PTR_ERR(file);
    sock = file->f_data;

    if ((err = get_user_sockaddr(&ksockaddr, addr, addrlen)) < 0)
        goto out;

    err = sock->sock_ops->connect(sock, &ksockaddr, addrlen);
out:
    fput(file);
    return err;
}

SYSCALL_DECL2(listen, int, sockfd, int, backlog)
{
    struct file *file;
    struct socket *sock;
    int err;

    file = get_socket_file(sockfd);
    if (IS_ERR(file))
        return PTR_ERR(file);
    sock = file->f_data;

    err = sock->sock_ops->listen(sock, backlog);
    fput(file);
    return err;
}

static int put_user_sockaddr(const ksockaddr_t *kaddr, socklen_t len,
    struct sockaddr __user *addr, socklen_t __user *addrlen)
{
    socklen_t ulen;

    if (get_user(ulen, addrlen))
        return -EFAULT;
    if (copy_to_user(addr, kaddr, MIN(len, ulen)))
        return -EFAULT;
    if (put_user(len, addrlen))
        return -EFAULT;
    return 0;
}

static int copy_sockaddr_to_user(struct socket *sock,
    struct sockaddr __user *addr, socklen_t __user *addrlen)
{
    ksockaddr_t kaddr;
    int actual_len;

    actual_len = sock->sock_ops->getname(sock, &kaddr, 1);
    if (actual_len < 0)
        return actual_len;

    return put_user_sockaddr(&kaddr, actual_len, addr, addrlen);
}

SYSCALL_DECL3(accept, int, sockfd, struct sockaddr *, addr, socklen_t *, addrlen)
{
    struct file *file, *newfile;
    struct socket *sock, *new_sock;
    const struct socket_ops *ops;
    int fd, err;

    fd = get_unused_fd();
    if (fd < 0)
        return -EMFILE;

    file = get_socket_file(sockfd);
    if (IS_ERR(file)) {
        err = PTR_ERR(file);
        goto free_fd;
    }
    sock = file->f_data;

    new_sock = alloc_socket(sock->domain, sock->type);
    if (!new_sock) {
        err = -ENOMEM;
        goto put_file;
    }

    ops = READ_ONCE(sock->sock_ops);
    new_sock->sock_ops = ops;

    newfile = alloc_socket_file(new_sock);
    if (IS_ERR(newfile)) {
        err = PTR_ERR(newfile);
        kfree(new_sock);
        goto put_file;
    }

    err = ops->accept(sock, new_sock);
    if (err < 0)
        goto put_newfile;

    if (addr && addrlen) {
        err = copy_sockaddr_to_user(new_sock, addr, addrlen);
        if (err < 0) {
            err = -ECONNABORTED;
            goto put_newfile;
        }
    }

    new_sock->file = newfile;
    fd_install(fd, newfile);
    fput(file);
    return fd;

put_newfile:
    fput(newfile);
put_file:
    fput(file);
free_fd:
    put_unused_fd(fd);
    return err;
}

SYSCALL_DECL6(sendto, int, sockfd, const void *, buf, size_t, len, int, flags,
              const struct sockaddr *, dest_addr, socklen_t, addrlen)
{
    struct file *file;
    struct socket *sock;
    ksockaddr_t ksockaddr, *kaddr = NULL;
    long ret = -EOPNOTSUPP;

    file = get_socket_file(sockfd);
    if (IS_ERR(file))
        return PTR_ERR(file);
    sock = file->f_data;

    if (!sock->sock_ops->sendto)
        goto out;

    if (dest_addr && addrlen) {
        if ((ret = get_user_sockaddr(&ksockaddr, dest_addr, addrlen)) < 0)
            goto out;
        kaddr = &ksockaddr;
    }

    ret = sock->sock_ops->sendto(sock, buf, len, flags, kaddr, addrlen);
out:
    fput(file);
    return ret;
}

SYSCALL_DECL6(recvfrom, int, sockfd, void *, buf, size_t, len, int, flags,
              struct sockaddr *, src_addr, socklen_t *, addrlen)
{
    struct file *file;
    struct socket *sock;
    ksockaddr_t kaddr;
    socklen_t klen = 0;
    bool want_addr = src_addr && addrlen;
    long ret = -EOPNOTSUPP;

    file = get_socket_file(sockfd);
    if (IS_ERR(file))
        return PTR_ERR(file);
    sock = file->f_data;

    if (!sock->sock_ops->recvfrom)
        goto out;

    ret = sock->sock_ops->recvfrom(sock, buf, len, flags,
        want_addr ? &kaddr : NULL, &klen);
    if (ret >= 0 && want_addr) {
        int err = put_user_sockaddr(&kaddr, klen, src_addr, addrlen);
        if (err)
            ret = err;
    }
out:
    fput(file);
    return ret;
}
