#ifndef _LILAC_FDTABLE_H
#define _LILAC_FDTABLE_H

struct file;

struct fdtable {
    struct file **fdarray;
    unsigned int max;
    atomic_uint ref_count;
    spinlock_t lock;
};

struct fdtable * alloc_fdtable(unsigned int size);
struct file * get_file_handle(int fd);
int get_next_fd(struct fdtable *, struct file *);
int get_fd_exact_replace(struct fdtable *files, int fd, struct file *file);
int get_fd_start_at(struct fdtable *files, int start, struct file *file);
int __vfs_dup(int oldfd, int newfd);

#endif
