#ifndef _LILAC_FDTABLE_H
#define _LILAC_FDTABLE_H

#include <lilac/bitmap.h>

struct file;

struct fdtable {
    struct file **fdarray;
    unsigned long *close_on_exec;
    unsigned int max;
    atomic_uint ref_count;
    spinlock_t lock;
};

#define is_cloexec(files, fd) __test_bit((files)->close_on_exec, (fd))
#define set_cloexec(files, fd) __set_bit((files)->close_on_exec, (fd))
#define clear_cloexec(files, fd) __clear_bit((files)->close_on_exec, (fd))

struct fdtable * alloc_fdtable(unsigned int size);
struct file * get_file_handle(int fd);
int get_next_fd(struct fdtable *, struct file *);
int get_fd_exact_replace(struct fdtable *files, int fd, struct file *file);
int get_fd_start_at(struct fdtable *files, int start, struct file *file);

#endif
