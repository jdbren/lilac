#include <lilac/fs.h>
#include <lilac/fdtable.h>
#include <lilac/err.h>
#include <lilac/log.h>
#include <lilac/sched.h>

static int __set_fdtsize(struct fdtable *files, unsigned int size)
{
    if (size <= files->max)
        return files->max;
    if (files->max >= 1024 || size > 1024)
        return -EMFILE;

    void *tmp = krealloc(files->fdarray, sizeof(struct file*) * size);
    if (!tmp)
        return -ENOMEM;
    files->fdarray = tmp;
    for (size_t i = files->max; i < size; i++)
        files->fdarray[i] = NULL;
    files->max = size;
    klog(LOG_DEBUG, "Increased max file descriptors to %d\n", size);
    return files->max;
}

static int __get_fd_for(struct fdtable *files)
{
    unsigned int cur_max = files->max;

    for (size_t i = 0; i < cur_max; i++) {
        if (!files->fdarray[i]) {
            return i;
        }
    }

    if (files->max < 256) {
        int new_size = __set_fdtsize(files, files->max * 2);
        if (new_size < 0)
            return new_size;
        return cur_max;
    } else {
        klog(LOG_DEBUG, "__get_fd_for: No available file descriptors\n");
        return -EMFILE;
    }
}


int get_fd_exact_replace(struct fdtable *files, int fd, struct file *file)
{
    if (fd < 0 || (unsigned)fd >= 1024)
        return -EINVAL;

    acquire_lock(&files->lock);
    if (fd >= (int)files->max) {
        int new_size = __set_fdtsize(files, MIN(fd * 2, 1024));
        if (new_size < 0) {
            release_lock(&files->lock);
            return new_size;
        }
    }

    if (files->fdarray[fd]) {
        vfs_close(files->fdarray[fd]);
        files->fdarray[fd] = NULL;
    }

    files->fdarray[fd] = file;
    release_lock(&files->lock);
    return fd;
}

int get_fd_start_at(struct fdtable *files, int start, struct file *file)
{
    if (start < 0 || (unsigned)start >= 1023)
        return -EINVAL;

    acquire_lock(&files->lock);

    if (start >= (int)files->max) {
        int new_size = __set_fdtsize(files, MIN(start * 2, 1024));
        if (new_size < 0) {
            release_lock(&files->lock);
            return new_size;
        }
    }

    for (int i = start; i < (int)files->max; i++) {
        if (!files->fdarray[i]) {
            files->fdarray[i] = file;
            release_lock(&files->lock);
            return i;
        }
    }

    release_lock(&files->lock);
    klog(LOG_DEBUG, "get_fd_start_at: No available file descriptors\n");
    return -EMFILE;
}

int get_next_fd(struct fdtable *files, struct file *file)
{
    if (!files || !file)
        return -EINVAL;

    acquire_lock(&files->lock);
    int fd = __get_fd_for(files);
    if (fd < 0)
        goto unlock;

    files->fdarray[fd] = file;
unlock:
    release_lock(&files->lock);
    return fd;
}

struct file * get_file_handle(int fd)
{
    struct file *file;
    struct fdtable *files = &current->fs.files;

    acquire_lock(&files->lock);
    if (fd < 0 || (unsigned)fd >= files->max) {
        release_lock(&files->lock);
        return ERR_PTR(-EBADF);
    }
    file = files->fdarray[fd];
    release_lock(&files->lock);

    if (!file)
        return ERR_PTR(-EBADF);
    return file;
}
