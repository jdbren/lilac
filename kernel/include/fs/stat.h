#ifndef _FS_STAT_H
#define _FS_STAT_H

#include <lilac/types.h>

struct file;

struct stat {
    dev_t           st_dev;     /* ID of device containing file */
    ino_t           st_ino;     /* inode number */
    umode_t         st_mode;    /* protection */
    nlink_t         st_nlink;   /* number of hard links */
    uid_t           st_uid;     /* user ID of owner */
    gid_t           st_gid;     /* group ID of owner */
    dev_t           st_rdev;    /* device ID (if special file) */
    off_t           st_size;    /* total size, in bytes */
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    blksize_t       st_blksize; /* blocksize for file system I/O */
    blkcnt_t        st_blocks;  /* number of blocks allocated */
};

int vfs_stat(const struct file *f, struct stat *st);

#endif
