#ifndef _FS_H
#define _FS_H

#include <lilac/types.h>
#include <lilac/list.h>
#include <lilac/sync.h>

enum fs_type {
    NONE = -1,
    MSDOS,
    NTFS
};

struct file {
	spinlock_t		f_lock;
	unsigned int	f_mode;
	unsigned long	f_pos;
	unsigned int	f_flags;
	// struct fown_struct	f_owner;
	// const struct cred *f_cred;
	// struct file_ra_state f_ra;
	char *f_path;
	void *f_info; // fs specific data
	struct vfsmount *f_disk;
	const struct file_operations *f_op;
};

struct file_operations {
	long (*llseek)(struct file *, long, int);
    ssize_t (*read)(struct file *, void *, size_t);
	ssize_t (*write)(struct file *, const void *, size_t);
};

struct dir {
	unsigned int d_flags;
	struct dir *d_parent;	/* parent directory */
	char *d_name;
	void *d_info;
	struct lockref d_lockref;
	struct super_block *d_sb;	/* The root of the dentry tree */
	struct hlist_head d_children;	/* our children */
};


enum vnode_type {
    VNODE_FILE,
    VNODE_DIR
};

struct vnode {
    u8 disknum;
    u8 type;
    u16 streams;
    union {
        struct dir *d;
        struct file *f;
    };
};

struct vfsmount {
    unsigned int num;
    char *devname;
    struct fs_operations *ops;
    struct block_device *bdev;
	void *private;
};

struct fs_operations {
    int (*init_fs)(struct vfsmount *);
    int (*open)(const char *path, struct file *);
};

int open(const char *path, int flags, int mode);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);

int fs_mount(struct block_device *bdev, enum fs_type type);

#endif
