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
    char name[50];
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

int tmpfs_create(struct inode *parent, struct dentry *new_dentry, umode_t mode);
ssize_t tmpfs_read(struct file *file, void *buf, size_t cnt);
ssize_t tmpfs_write(struct file *file, const void *buf, size_t cnt);
int tmpfs_readdir(struct file *file, struct dirent *dirp, unsigned int count);
int tmpfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);
int tmpfs_open(struct inode *inode, struct file *file);
struct dentry *tmpfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags);

#endif
