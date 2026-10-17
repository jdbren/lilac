#ifndef _LILAC_FDTABLE_H
#define _LILAC_FDTABLE_H

#include <lilac/bitmap.h>

struct file;

#define FD_AUTO_MAX 1024
#define FD_MAX      1024

struct fdtable {
    struct file **fdarray;
    unsigned long *open_fds;        // allocated fds
    unsigned long *close_on_exec;
    unsigned int max;
    atomic_uint ref_count;
    spinlock_t lock;
};

#define is_cloexec(files, fd) __test_bit((files)->close_on_exec, (fd))
#define set_cloexec(files, fd) __set_bit((files)->close_on_exec, (fd))
#define clear_cloexec(files, fd) __clear_bit((files)->close_on_exec, (fd))
#define fd_is_open(files, fd) __test_bit((files)->open_fds, (fd))

struct fdtable * alloc_fdtable(unsigned int size);
struct fdtable * dup_fdtable(struct fdtable *src);
struct file * fget_handle(int fd);
struct file * fd_remove(struct fdtable *files, int fd);
void fd_install(int fd, struct file *file);
void put_unused_fd(int fd);

struct file * fget_pos(int fd, bool *locked);
void fput_pos(struct file *file, bool locked);
bool file_pos_lock(struct file *file);
void file_pos_unlock(struct file *file, bool locked);

int get_unused_fd_flags(unsigned int flags);
int get_unused_fd_from(int start, unsigned int flags);
int get_fd_exact_replace(struct fdtable *files, int fd, struct file *file);

static inline int get_unused_fd(void)
{
    return get_unused_fd_flags(0);
}

#endif
