#include <lilac/fs.h>
#include <lilac/fdtable.h>
#include <lilac/err.h>
#include <lilac/log.h>
#include <lilac/panic.h>
#include <lilac/sched.h>

struct fdtable * alloc_fdtable(unsigned int size)
{
    struct fdtable *files = kmalloc(sizeof(*files));
    if (!files)
        return NULL;
    files->fdarray = kcalloc(size, sizeof(struct file*));
    files->open_fds = kcalloc(BITS_TO_LONGS(size), sizeof(unsigned long));
    files->close_on_exec = kcalloc(BITS_TO_LONGS(size), sizeof(unsigned long));
    if (!files->fdarray || !files->open_fds || !files->close_on_exec) {
        kfree(files->fdarray);
        kfree(files->open_fds);
        kfree(files->close_on_exec);
        kfree(files);
        return NULL;
    }
    files->max = size;
    spin_lock_init(&files->lock);
    files->ref_count = 1;
    return files;
}

static int __set_fdtsize(struct fdtable *files, unsigned int size)
{
    if (size <= files->max)
        return files->max;
    if (files->max >= FD_MAX || size > FD_MAX)
        return -EMFILE;

    void *tmp = krealloc(files->fdarray, sizeof(struct file*) * size);
    if (!tmp)
        return -ENOMEM;
    files->fdarray = tmp;
    tmp = krealloc(files->open_fds, sizeof(unsigned long) * BITS_TO_LONGS(size));
    if (!tmp)
        return -ENOMEM;
    files->open_fds = tmp;
    tmp = krealloc(files->close_on_exec, sizeof(unsigned long) * BITS_TO_LONGS(size));
    if (!tmp)
        return -ENOMEM;
    files->close_on_exec = tmp;
    for (size_t i = files->max; i < size; i++) {
        files->fdarray[i] = NULL;
        __clear_bit(files->open_fds, i);
        clear_cloexec(files, i);
    }
    files->max = size;
    klog(LOG_DEBUG, "Increased max file descriptors to %d\n", size);
    return files->max;
}

// Claim the lowest free slot at or above start. Called with files->lock held.
static int __alloc_fd(struct fdtable *files, int start, unsigned int flags)
{
    if (start < 0 || (unsigned)start >= FD_MAX)
        return -EINVAL;

    for (;;) {
        for (unsigned int i = start; i < files->max; i++) {
            if (fd_is_open(files, i))
                continue;
            __set_bit(files->open_fds, i);
            if (flags & O_CLOEXEC)
                set_cloexec(files, i);
            else
                clear_cloexec(files, i);
            return i;
        }

        if (files->max >= FD_AUTO_MAX) {
            klog(LOG_DEBUG, "__alloc_fd: No available file descriptors\n");
            return -EMFILE;
        }
        unsigned int size = MAX(files->max * 2, (unsigned)start + 1);
        int err = __set_fdtsize(files, MIN(size, FD_AUTO_MAX));
        if (err < 0)
            return err;
    }
}

int get_unused_fd_from(int start, unsigned int flags)
{
    struct fdtable *files = current->files;
    acquire_lock(&files->lock);
    int fd = __alloc_fd(files, start, flags);
    release_lock(&files->lock);
    return fd;
}

int get_unused_fd_flags(unsigned int flags)
{
    return get_unused_fd_from(0, flags);
}

// Takes over the caller's reference to file
void fd_install(int fd, struct file *file)
{
    struct fdtable *files = current->files;
    acquire_lock(&files->lock);
    assert(fd_is_open(files, fd) && !files->fdarray[fd]);
    files->fdarray[fd] = file;
    release_lock(&files->lock);
}

void put_unused_fd(int fd)
{
    struct fdtable *files = current->files;
    acquire_lock(&files->lock);
    assert(fd_is_open(files, fd) && !files->fdarray[fd]);
    __clear_bit(files->open_fds, fd);
    release_lock(&files->lock);
}

int get_fd_exact_replace(struct fdtable *files, int fd, struct file *file)
{
    struct file *oldf = NULL;
    if (fd < 0 || (unsigned)fd >= FD_MAX)
        return -EINVAL;

    acquire_lock(&files->lock);
    if (fd >= (int)files->max) {
        int new_size = __set_fdtsize(files, MIN(fd * 2, FD_MAX));
        if (new_size < 0) {
            release_lock(&files->lock);
            return new_size;
        }
    }

    // Reserved
    if (fd_is_open(files, fd) && !files->fdarray[fd]) {
        release_lock(&files->lock);
        return -EBUSY;
    }

    oldf = files->fdarray[fd];
    files->fdarray[fd] = file;
    __set_bit(files->open_fds, fd);
    clear_cloexec(files, fd);
    release_lock(&files->lock);

    if (oldf)
        vfs_close(oldf);

    return fd;
}

int get_fd_start_at(struct fdtable *files, int start, struct file *file)
{
    acquire_lock(&files->lock);
    int fd = __alloc_fd(files, start, 0);
    if (fd >= 0)
        files->fdarray[fd] = file;
    release_lock(&files->lock);
    return fd;
}

int get_next_fd(struct fdtable *files, struct file *file)
{
    if (!files || !file)
        return -EINVAL;
    return get_fd_start_at(files, 0, file);
}

// fget_handle for syscalls that use f_pos (locks the file position)
struct file * fget_pos(int fd, bool *locked)
{
    struct file *file = fget_handle(fd);
    if (IS_ERR(file))
        return file;

    struct inode *inode = file->f_inode;
    *locked = inode && (S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode));
    if (*locked)
        mutex_lock(&file->f_pos_lock);
    return file;
}

void fput_pos(struct file *file, bool locked)
{
    if (locked)
        mutex_unlock(&file->f_pos_lock);
    fput(file);
}

struct file * fd_remove(struct fdtable *files, int fd)
{
    struct file *file = NULL;

    acquire_lock(&files->lock);
    if (fd >= 0 && (unsigned)fd < files->max) {
        file = files->fdarray[fd];
        files->fdarray[fd] = NULL;
        if (file)
            __clear_bit(files->open_fds, fd);
    }
    release_lock(&files->lock);

    if (!file)
        return ERR_PTR(-EBADF);
    return file;
}

struct file * fget_handle(int fd)
{
    struct file *file;
    struct fdtable *files = current->files;

    acquire_lock(&files->lock);
    if (fd < 0 || (unsigned)fd >= files->max) {
        release_lock(&files->lock);
        return ERR_PTR(-EBADF);
    }
    file = files->fdarray[fd];
    if (file)
        file->f_count++;
    release_lock(&files->lock);

    if (!file)
        return ERR_PTR(-EBADF);
    return file;
}
