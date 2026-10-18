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

SYSCALL_DECL3(socket, int, domain, int, type, int, protocol)
{
    int fd;
    int err = 0;
    struct socket *sock;
    struct file *file;
    struct inode *inode;

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

    inode = new_anon_inode(S_IFSOCK | S_IREAD | S_IWRITE, NULL);
    if (!inode) {
        err = -ENOMEM;
        goto err_sock;
    }

    file = alloc_anon_file(inode, O_RDWR, sock);
    if (!file) {
        iput(inode);
        err = -ENOMEM;
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
