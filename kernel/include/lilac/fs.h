#ifndef _FS_H
#define _FS_H

#include <lilac/config.h>
#include <lilac/types.h>
#include <lilac/list.h>
#include <lilac/list_bl.h>
#include <lilac/sync.h>
#include <lilac/file.h>
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
    atomic_uint 		i_count;
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

    dev_t i_rdev;
    struct file_operations *i_fop;

    void *i_private; /* fs or device private pointer */
};

struct __cacheline_align inode_operations {
    struct dentry *(*lookup)(struct inode *, struct dentry *, unsigned int);
    int (*open)(struct inode *, struct file *);
    int (*create)(struct inode *, struct dentry *, umode_t);
    int (*mkdir)(struct inode *, struct dentry *, umode_t);
    int (*rmdir)(struct inode *, struct dentry *);
    int (*mknod)(struct inode *, struct dentry *, umode_t, dev_t);
    int (*rename)(struct inode *, struct dentry *,
            struct inode *, struct dentry *, unsigned int);
    int (*update_time)(struct inode *, int);
};


#define d_lock	d_lockref.lock

struct dentry {
    atomic_uint d_count;
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

    struct vfsmount *d_mount;
};

struct __cacheline_align dentry_operations {
    int (*d_revalidate)(struct dentry *, unsigned int);
    int (*d_hash)(const struct dentry *, const char *);
    int (*d_delete)(const struct dentry *);
    int (*d_init)(struct dentry *);
};

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
    struct dentry *(*init_fs)(void*, struct super_block *);
};

int vfs_lseek(struct file *file, int offset, int whence);
struct file* vfs_open(const char *path, int flags, int mode);
ssize_t vfs_read(struct file *file, void *buf, size_t count);
ssize_t vfs_write(struct file *file, const void *buf, size_t count);
int vfs_close(struct file *file);
ssize_t vfs_getdents(struct file *file, struct dirent *dirp, unsigned int buf_size);
int vfs_create(const char *path, umode_t mode);
int vfs_mkdir(const char *path, umode_t mode);
int vfs_mount(const char *source, const char *target,
        const char *filesystemtype, unsigned long mountflags,
        const void *data);
int vfs_umount(const char *target);

struct dentry * vfs_lookup(const char *path);

void fs_init(void);
struct dentry *mount_bdev(struct block_device *bdev, int (*fill_super)(struct super_block*));

struct super_block * alloc_sb(enum fs_type type);
void destroy_sb(struct super_block *sb);

struct dentry * lookup_path_from(struct dentry *parent, const char *path);
struct dentry * lookup_path(const char *path);
struct dentry * dlookup(struct dentry *parent, const char *name);

void dget(struct dentry *d);
void dput(struct dentry *d);
struct dentry * alloc_dentry(struct dentry *d_parent, const char *name);
void destroy_dentry(struct dentry *d);
void dcache_add(struct dentry *d);
void dcache_remove(struct dentry *d);

void iget(struct inode *inode);
void iput(struct inode *inode);

void fget(struct file *file);
void fput(struct file *file);

typedef struct dentry *(*fs_init_func_t)(void*, struct super_block*);

struct dentry * get_root_dentry(void);
struct vfsmount * get_empty_vfsmount(enum fs_type type);
fs_init_func_t get_fs_init(enum fs_type type);

int get_next_fd(struct fdtable *, struct file *);

#endif
