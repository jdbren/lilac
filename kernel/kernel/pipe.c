#include <lilac/pipe.h>
#include <lilac/lilac.h>
#include <lilac/fs.h>
#include <lilac/syscall.h>
#include <mm/kmm.h>
#include <mm/page.h>
#include <lilac/uaccess.h>
#include <lilac/timer.h>
#include <lilac/sched.h>

#define PIPE_BUF 4096

ssize_t pipe_read(struct file *f, void *buf, size_t count);
ssize_t pipe_write(struct file *f, const void *buf, size_t count);
int pipe_close(struct inode *i, struct file *f);
poll_mask_t pipe_poll(struct file *f, struct poll_request *p);

static const struct file_operations pipe_fops = {
    .read = pipe_read,
    .write = pipe_write,
    .release = pipe_close,
    .poll = pipe_poll,
};

struct pipe_buf * create_pipe(size_t buf_size)
{
    buf_size = PAGE_ROUND_UP(buf_size);
    klog(LOG_DEBUG, "Creating pipe with buffer size %lu\n", buf_size);
    struct pipe_buf *p = kzmalloc(sizeof(*p));
    if (!p) {
        klog(LOG_ERROR, "Failed to allocate pipe structure\n");
        return ERR_PTR(-ENOMEM);
    }

    p->buffer = get_free_pages(buf_size / PAGE_SIZE, 0);
    if (!p->buffer) {
        klog(LOG_ERROR, "Failed to allocate pipe buffer\n");
        kfree(p);
        return ERR_PTR(-ENOMEM);
    }

    p->buf_size = buf_size;
    INIT_LIST_HEAD(&p->read_wq.task_list);
    INIT_LIST_HEAD(&p->write_wq.task_list);
    p->n_readers = 1;
    p->n_writers = 1;
    p->files = 2;
#ifdef DEBUG_PIPE
    klog(LOG_DEBUG, "Pipe created successfully: %p\n", p);
#endif
    return p;
}

void destroy_pipe(struct pipe_buf *p)
{
    if (!p)
        return;
#ifdef DEBUG_PIPE
    klog(LOG_DEBUG, "Destroying pipe %p\n", p);
#endif
    if (p->buffer)
        free_pages(p->buffer, p->buf_size / PAGE_SIZE);

    kfree(p);
}

static void ring_copy_out(struct pipe_buf *pipe, void *dst, unsigned int pos, size_t n)
{
    size_t first = MIN(n, (size_t)(pipe->buf_size - pos));
    memcpy(dst, pipe->buffer + pos, first);
    memcpy((u8 *)dst + first, pipe->buffer, n - first);
}

static void ring_copy_in(struct pipe_buf *pipe, unsigned int pos, const void *src, size_t n)
{
    size_t first = MIN(n, (size_t)(pipe->buf_size - pos));
    memcpy(pipe->buffer + pos, src, first);
    memcpy(pipe->buffer, (const u8 *)src + first, n - first);
}

ssize_t pipe_read(struct file *f, void *buf, size_t count)
{
    if (count == 0 || !buf || !f)
        return 0;

    struct pipe_buf *pipe = f->f_inode->i_pipe;
    int pos, to_read;
    if (!pipe) {
        klog(LOG_ERROR, "pipe_read: Invalid pipe buffer\n");
        return -EIO;
    }

#ifdef DEBUG_PIPE
    klog(LOG_DEBUG, "pipe_read: Reading %lu bytes from pipe %p\n", count, pipe);
#endif
    acquire_lock(&pipe->lock);
    while (pipe->data_size == 0) {
        if (pipe->n_writers == 0) {
            release_lock(&pipe->lock);
            klog(LOG_DEBUG, "pipe_read: No writers, returning 0 bytes\n");
            return 0; // EOF
        }
        release_lock(&pipe->lock);
        if (f->f_mode & O_NONBLOCK)
            return -EAGAIN;
        int err = wait_event_interruptible(pipe->read_wq,
                READ_ONCE(pipe->data_size) != 0 || READ_ONCE(pipe->n_writers) == 0);
        if (err)
            return err;
        acquire_lock(&pipe->lock);
    }

    pos = pipe->idx.read_pos;
    to_read = MIN(count, pipe->data_size);
    ring_copy_out(pipe, buf, pos, to_read);
    pipe->idx.read_pos = (pos + to_read) % pipe->buf_size;
    pipe->data_size -= to_read;

    release_lock(&pipe->lock);
    wake_all(&pipe->write_wq);
    return to_read;
}

ssize_t pipe_write(struct file *f, const void *buf, size_t count)
{
    if (count == 0 || !buf || !f)
        return 0;
    struct pipe_buf *pipe = f->f_inode->i_pipe;
    if (!pipe) {
        klog(LOG_ERROR, "pipe_write: Invalid pipe buffer\n");
        return -EIO;
    }
#ifdef DEBUG_PIPE
    klog(LOG_DEBUG, "pipe_write: Writing %lu bytes to pipe %p\n", count, pipe);
#endif

    // Free space needed before copying: all of a small write, any for a large one
    const unsigned int min_space = count <= PIPE_BUF ? MIN(count, pipe->buf_size) : 1;
    size_t written = 0;
    ssize_t err = 0;

    while (written < count) {
        acquire_lock(&pipe->lock);

        if (pipe->n_readers == 0) {
            release_lock(&pipe->lock);
            do_raise(current, SIGPIPE);
            err = -EPIPE;
            break;
        }

        unsigned int space = pipe->buf_size - pipe->data_size;
        if (space < min_space) {
            release_lock(&pipe->lock);
            if (f->f_mode & O_NONBLOCK) {
                err = -EAGAIN;
                break;
            }
            err = wait_event_interruptible(pipe->write_wq,
                    pipe->buf_size - READ_ONCE(pipe->data_size) >= min_space ||
                    READ_ONCE(pipe->n_readers) == 0);
            if (err)
                break;
            continue;
        }

        unsigned int pos = pipe->idx.write_pos;
        unsigned int n = MIN(count - written, space);
        ring_copy_in(pipe, pos, (const u8 *)buf + written, n);
        pipe->idx.write_pos = (pos + n) % pipe->buf_size;
        pipe->data_size += n;
        written += n;
        release_lock(&pipe->lock);

        wake_all(&pipe->read_wq);
    }

    return written ? (ssize_t)written : err;
}

int pipe_close(struct inode *i, struct file *f)
{
    if (!f || !f->f_inode || !f->f_inode->i_pipe)
        return -EINVAL;

    struct pipe_buf *p = f->f_inode->i_pipe;
    mode_t accmode = f->f_mode & O_ACCMODE;
    acquire_lock(&p->lock);

    if (accmode == O_WRONLY || accmode == O_RDWR) {
        p->n_writers--;
        // Blocked readers must see EOF
        if (p->n_writers == 0)
            wake_all(&p->read_wq);
    }
    if (accmode == O_RDONLY || accmode == O_RDWR) {
        p->n_readers--;
        if (p->n_readers == 0)
            wake_all(&p->write_wq);
    }

    unsigned int files = --p->files;
    release_lock(&p->lock);

    klog(LOG_DEBUG, "pipe_close: Closed pipe %p, remaining files: %u\n", p, files);

    if (files == 0)
        destroy_pipe(p);

    return 0;
}

poll_mask_t pipe_poll(struct file *f, struct poll_request *pollreq)
{
    poll_mask_t mask;
    struct pipe_buf *pipe = f->f_inode->i_pipe;
    unsigned int size;
    mode_t accmode = f->f_mode & O_ACCMODE;

    if (accmode == O_RDONLY || accmode == O_RDWR)
        poll_wait(f, &pipe->read_wq, pollreq);
    if (accmode == O_WRONLY || accmode == O_RDWR)
        poll_wait(f, &pipe->write_wq, pollreq);

    size = READ_ONCE(pipe->data_size);

    mask = 0;
    if (accmode == O_RDONLY || accmode == O_RDWR) {
        if (size != 0)
            mask |= POLLIN | POLLRDNORM;
        if (!pipe->n_writers) // && f->f_pipe != pipe->w_counter)
            mask |= POLLHUP;
    }

    if (accmode == O_WRONLY || accmode == O_RDWR) {
        if (size < pipe->buf_size)
            mask |= POLLOUT | POLLWRNORM;
        if (!pipe->n_readers)
            mask |= POLLERR;
    }

    return mask;
}

static long do_pipe2(int __user *pipefd, int flags)
{
    struct file *read_end, *write_end;
    struct inode *inode;
    struct pipe_buf *p;
    int rfd, wfd;
    long err;

    if (flags & ~(O_CLOEXEC | O_NONBLOCK))
        return -EINVAL;
    if (!access_ok(pipefd, 2 * sizeof(int)))
        return -EFAULT;

    p = create_pipe(PAGE_SIZE);
    if (IS_ERR(p))
        return PTR_ERR(p);

    inode = new_anon_inode(S_IFIFO|S_IREAD|S_IWRITE, &pipe_fops);
    if (!inode) {
        destroy_pipe(p);
        return -ENOMEM;
    }
    inode->i_pipe = p;
    inode->i_size = p->buf_size;
    p->p_inode = inode;

    // One inode reference for each end
    iget(inode);
    read_end = alloc_anon_file(inode, O_RDONLY | flags, p);
    write_end = alloc_anon_file(inode, O_WRONLY | flags, p);
    if (!read_end || !write_end) {
        err = -ENOMEM;
        goto free_files;
    }

    // Reserve both fds and copy them out before either becomes visible
    rfd = get_unused_fd_flags(flags);
    if (rfd < 0) {
        err = rfd;
        goto free_files;
    }
    wfd = get_unused_fd_flags(flags);
    if (wfd < 0) {
        err = wfd;
        goto put_rfd;
    }

    if (put_user(rfd, &pipefd[0]) || put_user(wfd, &pipefd[1])) {
        err = -EFAULT;
        goto put_wfd;
    }

    fd_install(rfd, read_end);
    fd_install(wfd, write_end);
    klog(LOG_DEBUG, "pipe: Created pipe with fds %d (read) and %d (write)\n", rfd, wfd);
    return 0;

put_wfd:
    put_unused_fd(wfd);
put_rfd:
    put_unused_fd(rfd);
free_files:
    kfree(read_end);
    kfree(write_end);
    iput(inode);
    iput(inode);
    destroy_pipe(p);
    return err;
}

SYSCALL_DECL1(pipe, int __user *, pipefd)
{
    return do_pipe2(pipefd, 0);
}

SYSCALL_DECL2(pipe2, int __user *, pipefd, int, flags)
{
    return do_pipe2(pipefd, flags);
}
