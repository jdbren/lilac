#ifndef _FS_STAT_H
#define _FS_STAT_H

#include <lilac/types.h>

struct file;

struct stat {
    dev_t           st_dev;     /* ID of device containing file */
    unsigned long   st_ino;     /* inode number */
    umode_t         st_mode;    /* protection */
    unsigned short  st_nlink;   /* number of hard links */
    uid_t           st_uid;     /* user ID of owner */
    gid_t           st_gid;     /* group ID of owner */
    dev_t           st_rdev;    /* device ID (if special file) */
    unsigned long   st_size;    /* total size, in bytes */
    unsigned int    st_blksize; /* blocksize for file system I/O */
    unsigned int    st_blocks;  /* number of 512B blocks allocated */
    time_t          st_atime;   /* time of last access */
    time_t          st_mtime;   /* time of last modification */
    time_t          st_ctime;   /* time of last status change */
};

int vfs_stat(const struct file *f, struct stat *st);

#endif
