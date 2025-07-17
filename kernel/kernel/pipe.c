#include <lilac/pipe.h>
#include <lilac/lilac.h>
#include <lilac/fs.h>
#include <lilac/syscall.h>
#include <mm/kmm.h>

ssize_t pipe_read(struct file *f, void *buf, size_t count);
ssize_t pipe_write(struct file *f, const void *buf, size_t count);
int pipe_close(struct inode *i, struct file *f);

const static struct file_operations pipe_fops = {
    .read = pipe_read,
    .write = pipe_write,
    .release = pipe_close,
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

    p->buffer = kvirtual_alloc(buf_size, PG_WRITE);
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

    return p;
}

void destroy_pipe(struct pipe_buf *p)
{
    if (!p)
        return;

    if (p->buffer)
        kvirtual_free(p->buffer, p->buf_size);

    kfree(p);
}

ssize_t pipe_read(struct file *f, void *buf, size_t count)
{
    if (count == 0 || !buf || !f)
        return 0;

    klog(LOG_DEBUG, "pipe_read: Reading %lu bytes from pipe %p\n", count, f);

    struct pipe_buf *pipe = f->pipe;
    int pos, to_read;
    if (!pipe) {
        klog(LOG_ERROR, "pipe_read: Invalid pipe buffer\n");
        return -EIO;
    }

    if (pipe->data_size == 0) {
        if (pipe->n_writers == 0) {
            klog(LOG_DEBUG, "pipe_read: No writers, returning 0 bytes\n");
            return 0; // EOF
        }
        sleep_on(&pipe->read_wq);
    }

    acquire_lock(&pipe->lock);

    pos = pipe->read_pos;
    to_read = MIN(count, pipe->data_size);
    memcpy(buf, pipe->buffer + pos, to_read);
    pipe->read_pos = (pos + to_read) % pipe->buf_size;
    pipe->data_size -= to_read;

    release_lock(&pipe->lock);
    wake_first(&pipe->write_wq);
    return to_read;
}

ssize_t pipe_write(struct file *f, const void *buf, size_t count)
{
    if (count == 0 || !buf || !f)
        return 0;
    struct pipe_buf *pipe = f->pipe;
    if (!pipe) {
        klog(LOG_ERROR, "pipe_read: Invalid pipe buffer\n");
        return -EIO;
    }

    klog(LOG_DEBUG, "pipe_write: Writing %lu bytes to pipe %p\n", count, f);

    if (pipe->data_size == pipe->buf_size) {
        sleep_on(&pipe->write_wq);
    }

    acquire_lock(&pipe->lock);
    int pos = pipe->write_pos;
    int to_write = MIN(count, pipe->buf_size - pipe->data_size);
    memcpy(pipe->buffer + pos, buf, to_write);
    pipe->write_pos = (pos + to_write) % pipe->buf_size;
    pipe->data_size += to_write;
    release_lock(&pipe->lock);

    wake_first(&pipe->read_wq);

    if (to_write != count) {
        klog(LOG_WARN, "pipe_write: Partial write (%d of %zu bytes)\n", to_write, count);
    }

    return to_write;
}

int pipe_close(struct inode *i, struct file *f)
{
    if (!f || !f->pipe)
        return -EINVAL;

    struct pipe_buf *p = f->pipe;
    acquire_lock(&p->lock);

    if (f->f_mode & O_WRONLY) {
        p->n_writers--;
    } else if (f->f_mode & O_RDONLY) {
        p->n_readers--;
    } else {
        klog(LOG_WARN, "pipe_close: Unknown pipe mode %o\n", f->f_mode);
    }

    if (--p->files == 0) {
        destroy_pipe(p);
        klog(LOG_DEBUG, "pipe_close: Pipe %p destroyed\n", p);
        goto out;
    }

    release_lock(&p->lock);
out:
    f->pipe = NULL;
    return 0;
}


SYSCALL_DECL1(pipe, int, pipefd[2])
{
    if (!access_ok(pipefd, 2 * sizeof(int)))
        return -EFAULT;

    struct pipe_buf *p = create_pipe(PAGE_SIZE);
    if (IS_ERR(p))
        return PTR_ERR(p);

    struct file *read_end = kzmalloc(sizeof(*read_end));
    struct file *write_end = kzmalloc(sizeof(*write_end));
    if (!read_end || !write_end) {
        destroy_pipe(p);
        kfree(read_end);
        kfree(write_end);
        return -ENOMEM;
    }
    fget(read_end);
    fget(write_end);

    read_end->f_op = &pipe_fops;
    read_end->f_mode = O_RDONLY;
    read_end->pipe = p;

    write_end->f_op = &pipe_fops;
    write_end->f_mode = O_WRONLY;
    write_end->pipe = p;

    int rfd = get_next_fd(&current->fs.files, read_end);
    int wfd = get_next_fd(&current->fs.files, write_end);
    if (rfd < 0 || wfd < 0) {
        destroy_pipe(p);
        kfree(read_end);
        kfree(write_end);
        return -EMFILE;
    }

    pipefd[0] = rfd;
    pipefd[1] = wfd;
    return 0;
}
