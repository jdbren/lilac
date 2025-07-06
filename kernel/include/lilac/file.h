#ifndef _LILAC_FILE_H
#define _LILAC_FILE_H

#include <lilac/config.h>
#include <lilac/types.h>
#include <lilac/sync.h>

struct dirent;
struct inode;
struct file_operations;

struct file {
    spinlock_t		f_lock;
    unsigned int	f_mode;
    atomic_ulong 	f_count;
    struct mutex	f_pos_lock;
    unsigned long	f_pos;
    // struct fown_struct	f_owner;
    struct dentry   *f_dentry;
    const struct file_operations *f_op;
    void *f_info; // fs specific data
    // struct vfsmount *f_disk;
};

struct file_operations {
    int     (*lseek)(struct file *, int, int);
    ssize_t (*read)(struct file *, void *, size_t);
    ssize_t (*write)(struct file *, const void *, size_t);
    int     (*readdir)(struct file *, struct dirent *, unsigned int);
    int     (*flush)(struct file *);
    int     (*release)(struct inode *, struct file *);
};

struct fdtable {
    struct file **fdarray;
    unsigned int max;
};

#endif
