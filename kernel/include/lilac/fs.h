#ifndef _FS_H
#define _FS_H

#include <lilac/config.h>
#include <lilac/types.h>
#include <lib/list.h>
// #include <lib/list_bl.h>
#include <lilac/sync.h>
#include <lilac/fdtable.h>
#include <fs/path.h>
#include <fs/types.h>
#include <fs/fcntl.h>

/**
 * Based on the Linux VFS structures
*/


struct file;
struct inode;
struct dentry;
struct super_block;
struct dirent;
struct vm_desc;

struct inode {
    umode_t             i_mode;
    uid_t               i_uid;
    gid_t               i_gid;
    const struct inode_operations *i_op;
    struct super_block *i_sb;

    spinlock_t          i_lock;    /* i_blocks, i_size */
    atomic_bool         i_dirty;
    unsigned int        i_flags;
    mutex_t             i_mutex;
    unsigned long       i_state;

    ino_t               i_ino;
    struct list_head    i_list; /* sb list of inodes */
    atomic_uint         i_count;
    unsigned int        i_nlink;
    u64                 i_size;
    u64                 i_atime;
    u64                 i_mtime;
    u64                 i_ctime;
    u32                 i_blocks;
    dev_t               i_rdev;

    struct hlist_node   i_hash;
    struct hlist_head   i_dentry;

    const struct file_operations *i_fop;

    void *i_private; /* fs or device private pointer */
};

struct __cacheline_align inode_operations {
    struct dentry *(*lookup)(struct inode *, struct dentry *, unsigned int);
    int (*open)(struct inode *, struct file *);
    int (*create)(struct inode *, struct dentry *, umode_t);
    int (*link)(struct dentry *,struct inode *,struct dentry *);
    int (*unlink)(struct inode *,struct dentry *);
    int (*symlink)(struct inode *,struct dentry *,const char *);
    int (*mkdir)(struct inode *, struct dentry *, umode_t);
    int (*rmdir)(struct inode *, struct dentry *);
    int (*mknod)(struct inode *, struct dentry *, umode_t, dev_t);
    int (*rename)(struct inode *, struct dentry *,
                    struct inode *, struct dentry *);
    int (*readlink) (struct dentry *, char __user *,int);
};


struct dentry {
    atomic_uint d_count;
    spinlock_t  d_lock;
    // struct hlist_bl_node d_hash; /* lookup hash list */
    struct dentry *d_parent;        /* parent directory */
    char *d_name;
    struct inode *d_inode;          /* Where the name belongs to - NULL is negative */

    const struct dentry_operations *d_op;
    struct super_block *d_sb;       /* The root of the dentry tree */
    unsigned long d_time;           /* used by d_revalidate */
    void *d_fsdata;                 /* fs-specific data */

    struct hlist_node d_sib;        /* child of parent list */
    struct hlist_head d_children;   /* our children */

    struct vfsmount *d_mount;
};

struct __cacheline_align dentry_operations {
    int (*d_revalidate)(struct dentry *, unsigned int);
    int (*d_hash)(const struct dentry *, const char *);
    int (*d_delete)(const struct dentry *);
    int (*d_init)(struct dentry *);
};

struct super_block {
    struct list_head    s_list;        /* Keep this first */
    unsigned long       s_blocksize;
    unsigned long long  s_maxbytes;    /* Max file size */
    enum fs_type        s_type;

    const struct super_operations *s_op;
    struct dentry       *s_root;
    struct semaphore    s_umount;
    atomic_uint         s_count;
    atomic_bool         s_active;

    struct block_device *s_bdev;
    struct file         *s_bdev_file;

    spinlock_t          s_lock;      /* Protects the sb and inode list */
    struct list_head    s_inodes;    /* all inodes for this fs */

    void *s_fs_info;    /* Filesystem private info */
};

struct writeback_control {
    unsigned long nr_to_write;
    unsigned long pages_skipped;
};

struct super_operations {
    struct inode *(*alloc_inode)(struct super_block *sb);
    void (*destroy_inode)(struct inode *);
    void (*dirty_inode) (struct inode *, int flags);
    int (*write_inode) (struct inode *, struct writeback_control *wbc);
    int (*drop_inode) (struct inode *);
    void (*evict_inode) (struct inode *);
    void (*put_super) (struct super_block *);
    int (*sync_fs)(struct super_block *sb, int wait);
};

/*
 * File types
 */
#define    DT_UNKNOWN     0
#define    DT_FIFO        1
#define    DT_CHR         2
#define    DT_DIR         4
#define    DT_BLK         6
#define    DT_REG         8
#define    DT_LNK        10
#define    DT_SOCK       12
#define    DT_WHT        14

struct dirent {
    unsigned long   d_ino;   /* file number of entry */
    unsigned long   d_off;      /* directory offset of entry */
    unsigned short  d_reclen;   /* length of this record */
    char            d_name[128];
    char            pad;
    char            d_type;
};

struct file {
    spinlock_t      f_lock;
    atomic_uint     f_count;
    mode_t          f_mode;
    atomic_uint     f_owner;
    off_t           f_pos;
    struct mutex    f_pos_lock;
    struct dentry  *f_dentry;
    struct inode   *f_inode;

    const struct file_operations *f_op;
    union {
        struct pipe_buf *pipe; // for pipe files
        void *f_data; // other fs specific data
    };
    struct vfsmount *f_disk;
};

struct file_operations {
    off_t   (*lseek)(struct file *, off_t, int);
    ssize_t (*read)(struct file *, void *, size_t);
    ssize_t (*write)(struct file *, const void *, size_t);
    int     (*readdir)(struct file *, struct dirent *, unsigned int);
    int     (*flush)(struct file *);
    int     (*release)(struct inode *, struct file *);
    int     (*ioctl)(struct file *, int op, void *args);
    int     (*mmap)(struct file *, struct vm_desc *);
};


struct vfsmount {
    struct dentry *mnt_root;    /* root of the mounted tree */
    struct super_block *mnt_sb;    /* pointer to superblock */
    int mnt_flags;
    struct dentry *(*init_fs)(void*, struct super_block *);
};

off_t vfs_lseek(struct file *file, off_t offset, int whence);
struct file* vfs_open(const char *path, int flags, int mode);
ssize_t vfs_read_at(struct file *file, void *buf, size_t count, unsigned long pos);
ssize_t vfs_read(struct file *file, void *buf, size_t count);
ssize_t vfs_write(struct file *file, const void *buf, size_t count);
int vfs_close(struct file *file);
ssize_t vfs_getdents(struct file *file, struct dirent *dirp, int buf_size);
int vfs_create(const char *path, umode_t mode);
int vfs_mkdir(const char *path, umode_t mode);
int vfs_mount(const char *source, const char *target,
        const char *filesystemtype, unsigned long mountflags,
        const void *data);
int vfs_umount(const char *target);
int vfs_dupf(int fd);
int vfs_dup(int oldfd, int newfd);

struct dentry * vfs_lookup(const char *path);
struct dentry * lookup_path_from(struct dentry *parent, const char *path);
struct dentry * lookup_path(const char *path);
struct dentry * dlookup(struct dentry *parent, char *name);

void fs_init(void);
struct dentry *mount_bdev(struct block_device *bdev, int (*fill_super)(struct super_block*));

struct super_block * alloc_sb(struct block_device *bdev);
void destroy_sb(struct super_block *sb);

struct dentry * alloc_dentry(struct dentry *d_parent, const char *name);
void dget(struct dentry *d);
void dput(struct dentry *d);
void destroy_dentry(struct dentry *d);
void dcache_add(struct dentry *d);
void dcache_remove(struct dentry *d);

struct inode * alloc_inode(struct super_block *sb);
void iget(struct inode *inode);
void iput(struct inode *inode);

struct file * alloc_file(struct dentry *d);
void fget(struct file *file);
void fput(struct file *file);

typedef struct dentry *(*fs_init_func_t)(void*, struct super_block*);

struct dentry * get_root_dentry(void);
struct vfsmount * get_empty_vfsmount(enum fs_type type);
fs_init_func_t get_fs_init(enum fs_type type);

#endif
