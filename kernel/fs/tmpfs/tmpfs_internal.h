#ifndef _TMPFS_INTERNAL_H
#define _TMPFS_INTERNAL_H

#include <lilac/types.h>
#include <lilac/sync.h>
#include <lilac/fs.h>

#define TMPFS_FILE      1
#define TMPFS_DIR       2
#define TMPFS_SYMLINK   3
#define TMPFS_PIPE      4

struct tmpfs_entry {
    struct inode *inode;
    unsigned short type;
    char name[NAME_MAX];
};

struct tmpfs_file {
    spinlock_t lock;
    void *data;
};

struct tmpfs_dir {
    spinlock_t lock;
    unsigned long num_entries;
    struct tmpfs_entry *children;
};

extern const struct super_operations tmpfs_sops;
extern const struct inode_operations tmpfs_iops;
extern const struct file_operations tmpfs_fops;

#endif
