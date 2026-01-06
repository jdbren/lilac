#include <lilac/pipe.h>
#include <lilac/lilac.h>
#include <lilac/fs.h>
#include <lilac/syscall.h>
#include <mm/kmm.h>
#include <mm/page.h>
#include <lilac/uaccess.h>
#include <lilac/timer.h>

ssize_t pipe_read(struct file *f, void *buf, size_t count);
ssize_t pipe_write(struct file *f, const void *buf, size_t count);
int pipe_close(struct inode *i, struct file *f);

static const struct file_operations pipe_fops = {
    .read = pipe_read,
    .write = pipe_write,
    .release = pipe_close,
};

struct inode * pipe_alloc_inode()
{
    struct inode *ino = kzmalloc(sizeof(*ino));
    if (!ino)
        return NULL;
    ino->i_mode = S_IFIFO|S_IREAD|S_IWRITE;
    ino->i_count = 1;
    ino->i_nlink = 1;
    ino->i_atime = ino->i_ctime = ino->i_mtime = get_unix_time();
    ino->i_fop = &pipe_fops;
    ino->i_size = PAGE_SIZE;
    return ino;
}

struct pipe_buf * create_pipe(size_t buf_size)
{
    buf_size = PAGE_ROUND_UP(buf_size);
    klog(LOG_DEBUG, "Creating pipe with buffer size %lu\n", buf_size);
    struct pipe_buf *p = kzmalloc(sizeof(*p));
    if (!p) {
        klog(LOG_ERROR, "Failed to allocate pipe structure\n");
        return ERR_PTR(-ENOMEM);
    }

    p->p_inode = pipe_alloc_inode();
    if (IS_ERR_OR_NULL(p->p_inode)) {
        kfree(p);
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
    if (p->p_inode)
        kfree(p->p_inode);

    if (p->buffer)
        free_pages(p->buffer, p->buf_size / PAGE_SIZE);

    kfree(p);
}

ssize_t pipe_read(struct file *f, void *buf, size_t count)
{
    if (count == 0 || !buf || !f)
        return 0;


    struct pipe_buf *pipe = f->pipe;
    int pos, to_read;
    if (!pipe) {
        klog(LOG_ERROR, "pipe_read: Invalid pipe buffer\n");
        return -EIO;
    }

#ifdef DEBUG_PIPE
    klog(LOG_DEBUG, "pipe_read: Reading %lu bytes from pipe %p\n", count, pipe);
#endif
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
        klog(LOG_ERROR, "pipe_write: Invalid pipe buffer\n");
        return -EIO;
    }

    if (pipe->n_readers == 0) {
        klog(LOG_WARN, "pipe_write: No readers, cannot write\n");
        do_raise(current, SIGPIPE);
        return -EPIPE;
    }
#ifdef DEBUG_PIPE
    klog(LOG_DEBUG, "pipe_write: Writing %lu bytes to pipe %p\n", count, pipe);
#endif
    if (pipe->data_size == pipe->buf_size) {
        sleep_on(&pipe->write_wq);
        if (pipe->n_readers == 0) {
            klog(LOG_WARN, "pipe_write: No readers after wake, raising SIGPIPE\n");
            do_raise(current, SIGPIPE);
            return -EPIPE;
        }
    }

    acquire_lock(&pipe->lock);
    int pos = pipe->write_pos;
    unsigned int to_write = MIN(count, pipe->buf_size - pipe->data_size);
    memcpy(pipe->buffer + pos, buf, to_write);
    pipe->write_pos = (pos + to_write) % pipe->buf_size;
    pipe->data_size += to_write;
    release_lock(&pipe->lock);

    wake_first(&pipe->read_wq);

    if (to_write != count) {
        klog(LOG_WARN, "pipe_write: Partial write (%d of %lu bytes)\n", to_write, count);
    }

    return to_write;
}

int pipe_close(struct inode *i, struct file *f)
{
    if (!f || !f->pipe)
        return -EINVAL;

    struct pipe_buf *p = f->pipe;
    acquire_lock(&p->lock);

    if ((f->f_mode & O_ACCMODE) == O_WRONLY) {
        p->n_writers--;
    } else if ((f->f_mode & O_ACCMODE) == O_RDONLY) {
        p->n_readers--;
        if (p->n_readers == 0)
            wake_all(&p->write_wq);
    } else {
        klog(LOG_ERROR, "pipe_close: Unknown pipe mode %o\n", f->f_mode);
    }

    p->files--;
    release_lock(&p->lock);

    klog(LOG_DEBUG, "pipe_close: Closed pipe %p, remaining files: %u\n", p, p->files);

    if (p->files == 0) {
        destroy_pipe(p);
    }

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

    struct file *read_end = alloc_file(NULL);
    struct file *write_end = alloc_file(NULL);
    if (!read_end || !write_end) {
        destroy_pipe(p);
        kfree(read_end);
        kfree(write_end);
        return -ENOMEM;
    }

    read_end->f_op = &pipe_fops;
    read_end->f_mode = O_RDONLY;
    read_end->pipe = p;
    read_end->f_inode = p->p_inode;

    write_end->f_op = &pipe_fops;
    write_end->f_mode = O_WRONLY;
    write_end->pipe = p;
    write_end->f_inode = p->p_inode;

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

    klog(LOG_DEBUG, "pipe: Created pipe with fds %d (read) and %d (write)\n", rfd, wfd);
    return 0;
}
