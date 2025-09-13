#ifndef _LILAC_FILE_H
#define _LILAC_FILE_H

#include <lilac/config.h>
#include <lilac/types.h>
#include <lilac/sync.h>

#define O_ACCMODE   000000003

#define O_RDONLY    000000000
#define O_WRONLY    000000001
#define O_RDWR      000000002

#define O_APPEND    000000010
#define O_CREAT     000001000
#define O_TRUNC     000002000
#define O_EXCL      000004000

#define O_NONBLOCK  00040000
#define O_NOCTTY    000100000

#define O_CLOEXEC   001000000
#define O_NOFOLLOW  004000000
#define O_DIRECTORY 010000000
#define O_EXEC      020000000
#define O_SEARCH    020000000


/* Encoding of the file mode.  */

#define S_IFMT      0170000 /* These bits determine file type.  */

/* File types.  */
#define S_IFDIR     0040000 /* Directory.  */
#define S_IFCHR     0020000 /* Character device.  */
#define S_IFBLK     0060000 /* Block device.  */
#define S_IFREG     0100000 /* Regular file.  */
#define S_IFIFO     0010000 /* FIFO.  */
#define S_IFLNK     0120000 /* Symbolic link.  */
#define S_IFSOCK    0140000 /* Socket.  */

/* Protection bits.  */

#define S_ISUID 04000 /* Set user ID on execution.  */
#define S_ISGID 02000 /* Set group ID on execution.  */
#define S_ISVTX 01000 /* Save swapped text after use (sticky).  */
#define S_IREAD  0400 /* Read by owner.  */
#define S_IWRITE 0200 /* Write by owner.  */
#define S_IEXEC  0100 /* Execute by owner.  */

struct dirent;
struct inode;
struct file_operations;
struct vfsmount;

struct file {
    spinlock_t  f_lock;
    unsigned int f_mode;
    atomic_ulong  f_count;
    struct mutex f_pos_lock;
    unsigned long f_pos;
    // struct fown_struct f_owner;
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

struct file * get_file_handle(int fd);

#endif
