#ifndef _FS_H
#define _FS_H

#include <lilac/config.h>
#include <lilac/types.h>
#include <lilac/list.h>
#include <lilac/list_bl.h>
#include <lilac/sync.h>
#include <fs/types.h>

/**
 * Based on the Linux VFS structures
*/

#define FS_CREATE 0x1


struct file;
struct inode;
struct dentry;
struct super_block;
struct dirent;

enum file_type {
    TYPE_FILE, TYPE_DIR, TYPE_DEV, TYPE_SPECIAL
};

struct inode {
    unsigned long 		i_ino;
    struct list_head 	i_list;
    umode_t				i_mode;
    unsigned short 		i_type;
    atomic_ulong 		i_count;
    const struct inode_operations *i_op;
    struct super_block *i_sb;

    u64			i_size;
    u64			i_atime;
    u64			i_mtime;
    u64			i_ctime;
    spinlock_t	i_lock;	/* i_blocks, i_size */
    u32			i_blocks;

    unsigned long		i_state;
    struct semaphore	i_rwsem;

    struct hlist_node	i_hash;
    struct hlist_head	i_dentry;

    void *i_private; /* fs or device private pointer */
};

struct inode_operations {
    struct dentry *(*lookup)(struct inode *, struct dentry *, unsigned int);
    int (*open)(struct inode *, struct file *);
    int (*create)(struct inode *, struct dentry *, umode_t, bool);
    int (*mkdir)(struct inode *, struct dentry *, umode_t);
    int (*rmdir)(struct inode *, struct dentry *);
    int (*mknod)(struct inode *, struct dentry *, umode_t, dev_t);
    int (*rename)(struct inode *, struct dentry *,
            struct inode *, struct dentry *, unsigned int);
    int (*update_time)(struct inode *, int);
} __cacheline_align;


#define d_lock	d_lockref.lock

struct dentry {
    // struct hlist_bl_node d_hash; /* lookup hash list */
    struct dentry *d_parent;	/* parent directory */
    char *d_name;
    struct inode *d_inode;		/* Where the name belongs to - NULL is negative */

    struct lockref d_lockref;	/* per-dentry lock and refcount */
    const struct dentry_operations *d_op;
    struct super_block *d_sb;	/* The root of the dentry tree */
    unsigned long d_time;		/* used by d_revalidate */
    void *d_fsdata;			/* fs-specific data */

    struct hlist_node d_sib;	/* child of parent list */
    struct hlist_head d_children;	/* our children */
};

struct dentry_operations {
    int (*d_revalidate)(struct dentry *, unsigned int);
    int (*d_hash)(const struct dentry *, const char *);
    int (*d_delete)(const struct dentry *);
    int (*d_init)(struct dentry *);
} __cacheline_align;

struct super_block {
    // struct list_head			s_list;		/* Keep this first */
    // dev_t					s_dev;		/* search index; _not_ kdev_t */
    unsigned long			s_blocksize;
    unsigned long long		s_maxbytes;	/* Max file size */
    enum fs_type			s_type;
    const struct super_operations	*s_op;
    struct dentry			*s_root;
    struct semaphore		s_umount;
    int						s_count;
    atomic_bool				s_active;

    struct block_device		*s_bdev;
    struct file				*s_bdev_file;

    spinlock_t				s_lock;		/* Protects the sb and inode list */
    struct list_head		s_inodes;	/* all inodes for this fs */

    void					*private;	/* Filesystem private info */
};

struct super_operations {
    struct inode *(*alloc_inode)(struct super_block *sb);
    void (*destroy_inode)(struct inode *);
    void (*dirty_inode)(struct inode *, int flags);
    int (*write_inode)(struct inode *);
    void (*shutdown)(struct super_block *sb);
};

struct dirent {
    int             d_ino;       /* Inode number */
    unsigned short  d_reclen;    /* Length of this record */
    char            d_name[32]; /* Null-terminated filename */
};

struct vfsmount {
    struct dentry *mnt_root;	/* root of the mounted tree */
    struct super_block *mnt_sb;	/* pointer to superblock */
    int mnt_flags;
    struct dentry *(*init_fs)(struct block_device *, struct super_block *);
};

int lseek(int fd, int offset, int whence);
int open(const char *path, int flags, int mode);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int getdents(unsigned int fd, struct dirent *dirp, unsigned int buf_size);

int mount(const char *source, const char *target,
        const char *filesystemtype, unsigned long mountflags,
        const void *data);

int fs_init(void);
struct dentry *mount_bdev(struct block_device *bdev,
    int (*fill_super)(struct super_block*));


struct inode * lookup_path(const char *path);
struct dentry * dlookup(struct dentry *parent, const char *name);
void dcache_add(struct dentry *d);

struct inode * get_root_inode(void);

#endif
