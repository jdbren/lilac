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
    if (!file->f_inode || !S_ISSOCK(file->f_inode->i_mode))
        return ERR_PTR(-ENOTSOCK);
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

void init_sock_internal(struct socket *sock, struct sock_internal *si)
{
    sock->si = si;
    si->sock = sock;
    spin_lock_init(&si->lock);
    si->cstate = C_CLOSE;
}

static struct file * alloc_socket_file(struct socket *sock)
{
    struct inode *inode = new_anon_inode(S_IFSOCK | S_IREAD | S_IWRITE, NULL);
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
        goto err_sock;
    }

    err = init_socket(sock, domain, type);
    if (err) {
        fput(file);
        goto err_sock;
    }

    sock->file = file;
    fd_install(fd, file);
    return fd;

err_sock:
    put_unused_fd(fd);
    kfree(sock);
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

static int copy_sockaddr_to_user(struct socket *sock,
    struct sockaddr __user *addr, socklen_t __user *addrlen)
{
    ksockaddr_t kaddr;
    socklen_t klen;
    int actual_len;

    if (get_user(klen, addrlen))
        return -EFAULT;

    actual_len = sock->sock_ops->getname(sock, &kaddr, 1);
    if (actual_len < 0)
        return actual_len;

    if (copy_to_user(addr, &kaddr, MIN((socklen_t)actual_len, klen)))
        return -EFAULT;
    if (put_user(actual_len, addrlen))
        return -EFAULT;

    return 0;
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
        goto free_sock;
    }

    err = init_socket(new_sock, sock->domain, sock->type);
    if (err < 0)
        goto put_newfile;

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
free_sock:
    kfree(new_sock);
put_file:
    fput(file);
free_fd:
    put_unused_fd(fd);
    return err;
}

SYSCALL_DECL4(send, int, sockfd, const void *, buf, size_t, len, int, flags)
{
    struct file *file;
    struct socket *sock;
    int err;

    file = get_socket_file(sockfd);
    if (IS_ERR(file))
        return PTR_ERR(file);
    sock = file->f_data;

    err = sock->sock_ops->send(sock, buf, len, flags);
    fput(file);
    return err;
}

SYSCALL_DECL4(recv, int, sockfd, void *, buf, size_t, len, int, flags)
{
    struct file *file;
    struct socket *sock;
    int err;

    file = get_socket_file(sockfd);
    if (IS_ERR(file))
        return PTR_ERR(file);
    sock = file->f_data;

    err = sock->sock_ops->recv(sock, buf, len, flags);
    fput(file);
    return err;
}

SYSCALL_DECL6(sendto, int, sockfd, const void *, buf, size_t, len, int, flags,
              const struct sockaddr *, dest_addr, socklen_t, addrlen)
{
    if (dest_addr)
        return -EOPNOTSUPP;
    return sys_send(sockfd, buf, len, flags);
}

SYSCALL_DECL6(recvfrom, int, sockfd, void *, buf, size_t, len, int, flags,
              struct sockaddr *, src_addr, socklen_t *, addrlen)
{
    long ret = sys_recv(sockfd, buf, len, flags);
    if (ret >= 0 && src_addr && addrlen) {
        if (put_user((socklen_t)0, addrlen))
            return -EFAULT;
    }
    return ret;
}
