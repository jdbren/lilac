#ifndef _VFS_CPP_H
#define _VFS_CPP_H

#include <lilac/types.h>
#include <fs/types.h>

#include <atomic>

using std::atomic_flag;
using std::atomic_bool;
using std::atomic_ulong;
using std::atomic_uint;

struct block_device;

enum file_type {
    TYPE_FILE, TYPE_DIR, TYPE_DEV, TYPE_SPECIAL, TYPE_TTY
};

namespace vfs {

struct file;

struct super_block {
    struct list_head		s_list;		/* Keep this first */
    // dev_t				s_dev;		/* search index; _not_ kdev_t */
    unsigned long			s_blocksize;
    unsigned long long		s_maxbytes;	/* Max file size */
    enum fs_type			s_type;
    struct dentry			*s_root;
    // struct semaphore		s_umount;
    int						s_count;
    atomic_bool				s_active;

    struct block_device		*s_bdev;
    struct file				&s_bdev_file;

    atomic_flag				s_lock;		/* Protects the sb and inode list */
    struct list_head		s_inodes;	/* all inodes for this fs */

    void					*s_private;	/* Filesystem private info */
};

struct inode {
    unsigned long 		i_ino;
    struct list_head 	i_list;
    umode_t				i_mode;
    unsigned short 		i_type;
    atomic_uint 		i_count;
    super_block&        i_sb;

    u64			i_size;
    u64			i_atime;
    u64			i_mtime;
    u64			i_ctime;
    atomic_flag	i_lock = ATOMIC_FLAG_INIT;	/* i_blocks, i_size */
    u32			i_blocks;

    unsigned long		i_state;
    // struct semaphore	i_rwsem;

    struct hlist_node	i_hash;
    struct hlist_head	i_dentry;

    dev_t i_rdev;
    struct file_operations *i_fop = nullptr;
    void *i_private = nullptr; /* fs or device private pointer */
public:
    explicit inode(super_block& sb) : i_sb(sb) {}

    virtual dentry* lookup(dentry& d, unsigned int) = 0;
    virtual int open(file &f) = 0;
    virtual int create(dentry &newd, umode_t mode) {};
    virtual int mkdir(dentry &newd, umode_t mode) {};
    virtual int rmdir(dentry &remove) {};
    virtual int mknod(dentry &newd, umode_t mode, dev_t devno) {};
};

struct dentry {
    atomic_uint d_count;
    // struct hlist_bl_node d_hash; /* lookup hash list */
    dentry *d_parent;	/* parent directory */
    char *d_name;
    inode *d_inode;		/* Where the name belongs to - NULL is negative */

    // struct lockref d_lockref;	/* per-dentry lock and refcount */
    super_block *d_sb;	/* The root of the dentry tree */
    unsigned long d_time;		/* used by d_revalidate */
    void *d_fsdata;			/* fs-specific data */

    struct hlist_node d_sib;	/* child of parent list */
    struct hlist_head d_children;	/* our children */

    struct vfsmount *d_mount;
};

struct file_operations {
    int     (*lseek)(struct file *, int, int);
    ssize_t (*read)(struct file *, void *, size_t);
    ssize_t (*write)(struct file *, const void *, size_t);
    int     (*readdir)(struct file *, struct dirent *, unsigned int);
    int     (*flush)(struct file *);
    int     (*release)(struct inode *, struct file *);
    int     (*ioctl)(struct file *, int op, void *args);
};

struct file {
    atomic_flag  f_lock;
    unsigned int f_mode;
    atomic_ulong  f_count;
    // struct mutex f_pos_lock;
    unsigned long f_pos;
    // struct fown_struct f_owner;
    dentry *f_dentry;
    inode *f_inode;
    union {
        struct pipe_buf *pipe; // for pipe files
        void *f_data; // other fs specific data
    };
    struct vfsmount *f_disk;
public:
    virtual int lseek(int offset, int whence) {};
    virtual ssize_t read(void *, size_t) {};
    virtual ssize_t write(const void *, size_t) {};
    virtual int readdir(struct dirent *, unsigned int) {};
    virtual int flush() {};
    virtual int release() {};
    virtual int ioctl(int op, void *args) {};
};

struct dirent {
	unsigned long   d_ino;		/* file number of entry */
	long            d_off;		/* directory offset of entry */
	unsigned short  d_reclen;	/* length of this record */
	unsigned char   d_type;		/* file type, see below */
	unsigned short  d_namlen;	/* length of string in d_name */
	char            d_name[64];	/* name must be no longer than this */
};

struct vfsmount {
    struct dentry *mnt_root;	/* root of the mounted tree */
    struct super_block *mnt_sb;	/* pointer to superblock */
    int mnt_flags;
    struct dentry *(*init_fs)(void*, struct super_block *);
};

extern "C" {
int lseek(file *f, int offset, int whence);
file* vfs_open(const char *path, int flags, int mode);
ssize_t vfs_read(file *file, void *buf, size_t count);
ssize_t vfs_write(file *file, const void *buf, size_t count);
int vfs_close(file *file);
ssize_t vfs_getdents(file *file, struct dirent *dirp, unsigned int buf_size);
int vfs_create(const char *path, umode_t mode);
int vfs_mkdir(const char *path, umode_t mode);
int vfs_mount(const char *source, const char *target,
        const char *filesystemtype, unsigned long mountflags,
        const void *data);
int vfs_umount(const char *target);
int vfs_dupf(int fd);
int vfs_dup(int oldfd, int newfd);

struct dentry * vfs_lookup(const char *path);

void fs_init(void);
struct dentry *mount_bdev(struct block_device *bdev, int (*fill_super)(struct super_block*));

struct super_block * alloc_sb(struct block_device *bdev);
void destroy_sb(struct super_block *sb);

struct dentry * lookup_path_from(struct dentry *parent, const char *path);
struct dentry * lookup_path(const char *path);
struct dentry * dlookup(struct dentry *parent, char *name);

void dget(struct dentry *d);
void dput(struct dentry *d);
struct dentry * alloc_dentry(struct dentry *d_parent, const char *name);
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
}

}



#endif
