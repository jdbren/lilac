#ifndef _LILAC_FILE_H
#define _LILAC_FILE_H

#include <lilac/config.h>
#include <lilac/types.h>
#include <lilac/sync.h>

#define O_ACCMODE	00000003
#define O_RDONLY	00000000
#define O_WRONLY	00000001
#define O_RDWR		00000002
#define O_CREAT		00000100	/* not fcntl */
#define O_EXCL		00000200	/* not fcntl */
#define O_NOCTTY	00000400	/* not fcntl */
#define O_TRUNC		00001000	/* not fcntl */
#define O_APPEND	00002000
#define O_NONBLOCK	00004000

struct dirent;
struct inode;
struct file_operations;
struct vfsmount;

struct file {
    spinlock_t		f_lock;
    unsigned int	f_mode;
    atomic_ulong 	f_count;
    struct mutex	f_pos_lock;
    unsigned long	f_pos;
    // struct fown_struct	f_owner;
    struct dentry   *f_dentry;
    const struct file_operations *f_op;
    union {
        struct pipe_buf *pipe; // for pipe files
        void *f_data; // other fs specific data
    };
    struct vfsmount *f_disk;
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
