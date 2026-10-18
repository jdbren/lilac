#include <lilac/socket.h>
#include <lilac/fdtable.h>
#include <lilac/fs.h>
#include <lilac/syscall.h>
#include <lilac/uaccess.h>
#include <mm/kmalloc.h>

static struct socket * alloc_socket(int domain, int type)
{
    struct socket *sock = kmalloc(sizeof(struct socket));
    if (!sock)
        return NULL;
    sock->state = SS_FREE;
    sock->domain = domain;
    sock->type = type;
    sock->file = NULL;
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

    if (domain <= 0 || domain > AF_MAX)
        return -EINVAL;
    if (type <= 0 || type > SOCK_MAX)
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

static char * get_user_sockaddr(const struct sockaddr *addr, socklen_t addrlen)
{
    if (addrlen < sizeof(struct sockaddr) || addrlen > MAX_SOCKADDR_LEN)
        return ERR_PTR(-EINVAL);

    const int bytes = addrlen - sizeof(addr->sa_family);

    char *ksockaddr = kmalloc(bytes + 1);
    if (!ksockaddr)
        return ERR_PTR(-ENOMEM);

    if (copy_from_user(ksockaddr, addr->sa_data, bytes)) {
        kfree(ksockaddr);
        return ERR_PTR(-EFAULT);
    }
    ksockaddr[bytes] = '\0';

    return ksockaddr;
}

SYSCALL_DECL3(bind, int, sockfd, const struct sockaddr *, addr, socklen_t, addrlen)
{
    struct file *file;
    struct inode *inode;
    struct socket *sock;
    int err;

    file = fget_handle(sockfd);
    if (IS_ERR_OR_NULL(file))
        return -EBADF;
    inode = file->f_inode;
    if (!inode || !S_ISSOCK(inode->i_mode)) {
        err = -ENOTSOCK;
        goto out;
    }

    sock = file->f_data;
    if (!sock) {
        err = -ENOTSOCK;
        goto out;
    }

    char *ksockaddr = get_user_sockaddr(addr, addrlen);
    if (IS_ERR(ksockaddr)) {
        err = PTR_ERR(ksockaddr);
        goto out;
    }

    err = sock->sock_ops->bind(sock, ksockaddr, addrlen - sizeof(addr->sa_family));
    kfree(ksockaddr);
out:
    fput(file);
    return err;
}
