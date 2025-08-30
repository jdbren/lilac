// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/fs.h>

#include <lilac/lilac.h>
#include <lilac/libc.h>
#include <lilac/syscall.h>
#include <lilac/device.h>
#include <lilac/sched.h>
#include <lilac/timer.h>
#include <lilac/uaccess.h>
#include <drivers/blkdev.h>
#include <fs/fat32.h>
#include <fs/tmpfs.h>

#include "utils.h"

// #define DEBUG_VFS 1

static fs_init_func_t init_ops[4] = {
    fat32_init, NULL, tmpfs_init, NULL
};

struct dentry *root_dentry = NULL;

fs_init_func_t get_fs_init(enum fs_type type)
{
    return init_ops[type];
}

struct dentry * get_root_dentry(void)
{
    return root_dentry;
}

static int get_fd_for(struct fdtable *files)
{
    for (size_t i = 0; i < files->max; i++) {
        if (!files->fdarray[i]) {
            return i;
        }
    }

    if (files->max < 256) {
        unsigned int new_max = files->max * 2;
        void *tmp = krealloc(files->fdarray, sizeof(struct file*) * new_max);
        if (!tmp)
            return -ENOMEM;
        files->fdarray = tmp;
        for (size_t i = files->max; i < new_max; i++)
            files->fdarray[i] = NULL;
        int fd = files->max;
        files->max = new_max;
        klog(LOG_DEBUG, "Increased max file descriptors to %d\n", new_max);
        return fd;
    } else {
        return -EMFILE;
    }
}

static int get_fd(void)
{
    struct fdtable *files = &current->fs.files;
    return get_fd_for(files);
}


int get_next_fd(struct fdtable *files, struct file *file)
{
    if (!files || !file)
        return -EINVAL;

    int fd = get_fd_for(files);
    if (fd < 0)
        return fd;

    files->fdarray[fd] = file;
    return fd;
}

static struct file * get_file_handle(int fd)
{
    struct file *file;
    struct fdtable *files = &current->fs.files;

    if (fd < 0 || (unsigned)fd >= files->max)
        return ERR_PTR(-EBADF);
    file = files->fdarray[fd];
    if (!file)
        return ERR_PTR(-EBADF);
    return file;
}

static void root_init(struct block_device *bdev)
{
    struct super_block *sb = alloc_sb(bdev);
    struct vfsmount *root_disk = get_empty_vfsmount(bdev->type);

    root_disk->mnt_sb = sb;

    root_dentry = root_disk->init_fs(bdev, sb);
    if (!root_dentry)
        kerror("Failed to initialize root dentry\n");
    root_disk->mnt_root = root_dentry;

    root_dentry->d_parent = NULL;
    root_dentry->d_name = kmalloc(2);
    strcpy(root_dentry->d_name, "/");
    root_dentry->d_mount = root_disk;
    INIT_HLIST_HEAD(&root_dentry->d_children);
    INIT_HLIST_NODE(&root_dentry->d_sib);

    klog(LOG_INFO, "%s mounted on /\n", bdev->name);
}

void fs_init(void)
{
    dev_t devt = DEVICE_ID(SATA_DEVICE, 0);
    struct block_device *bdev;

    scan_partitions(NULL);
    bdev = get_bdev(devt);
    if (!bdev)
        kerror("Failed to get root block device\n");

    root_init(bdev);

    vfs_mount("tmpfs", "/tmp", "tmpfs", 0, NULL);
    vfs_mount("tmpfs", "/dev", "tmpfs", 0, NULL);
    vfs_create("/dev/null", 0);
    vfs_create("/dev/zero", 0);

    kstatus(STATUS_OK, "Filesystem initialized\n");
}


#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int vfs_lseek(struct file *file, int offset, int whence)
{
    if (whence == SEEK_SET)
        file->f_pos = offset;
    else if (whence == SEEK_CUR)
        file->f_pos += offset;
    else if (whence == SEEK_END)
        file->f_pos = file->f_dentry->d_inode->i_size + offset;
    else
        return -EINVAL;

    return file->f_pos;
}
SYSCALL_DECL3(lseek, int, fd, int, offset, int, whence)
{
    struct file *file = get_file_handle(fd);
    if (IS_ERR(file))
        return PTR_ERR(file);
    return vfs_lseek(file, offset, whence);
}

struct dentry * vfs_lookup(const char *path)
{
    struct dentry *start = root_dentry;
    if (*path == '\0')
        return ERR_PTR(-ENOENT);

    if (path[0] != '/')
        start = current->fs.cwd_d;

    if (!strcmp(path, "."))
        return start;

    if (!strcmp(path, "/"))
        return root_dentry;

    return lookup_path_from(start, path);
}

static int create_file_at(struct dentry *new, umode_t mode)
{
    klog(LOG_DEBUG, "Creating file %s with mode %o\n", new->d_name, mode);
    struct inode *parent_inode = new->d_parent->d_inode;
    return parent_inode->i_op->create(parent_inode, new, mode);
}

struct file *vfs_open(const char *path, int flags, int mode)
{
    klog(LOG_DEBUG, "VFS: Opening %s, flags = %x, mode = %x\n", path, flags, mode);
    struct inode *inode;
    struct file *new_file;
    long err;

    struct dentry *dentry = vfs_lookup(path);
    if (IS_ERR(dentry))
        return ERR_CAST(dentry);
    inode = dentry->d_inode;
    if (!inode) {
        if (flags & O_CREAT) {
            int err = create_file_at(dentry, mode);
            if (err < 0) {
                klog(LOG_DEBUG, "Failed to create file %s: %d\n", path, err);
                return ERR_PTR(err);
            }
            inode = dentry->d_inode;
        } else {
            return ERR_PTR(-ENOENT);
        }
    }

    new_file = alloc_file(dentry);
    if (IS_ERR_OR_NULL(new_file))
        return ERR_PTR(-ENOMEM);

    fget(new_file);

    if ((err = inode->i_op->open(inode, new_file)) < 0) {
        fput(new_file);
        return ERR_PTR(err);
    }

    return new_file;
}
SYSCALL_DECL3(open, const char*, path, int, flags, int, mode)
{
    char *path_buf = kmalloc(256);
    struct file *f;
    long fd;

    if (!path_buf)
        return -ENOMEM;

    fd = strncpy_from_user(path_buf, path, 255);
    if (fd < 0)
        goto error;

    f = vfs_open(path_buf, flags, mode);
    if (IS_ERR(f)) {
        klog(LOG_DEBUG, "Failed to open %s\n", path_buf);
        fd = PTR_ERR(f);
        goto error;
    }

    fd = get_fd();
    if (fd < 0) {
        vfs_close(f);
        fd = -EMFILE;
        goto error;
    }

    current->fs.files.fdarray[fd] = f;
#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "VFS: Opened file %s with fd %d\n", path_buf, fd);
#endif
error:
    kfree(path_buf);
    return fd;
}


ssize_t vfs_read(struct file *file, void *buf, size_t count)
{
    if (file->f_dentry) {
        struct inode *inode = file->f_dentry->d_inode;
        if (inode->i_type == TYPE_DIR)
            return -EISDIR;

        if (inode->i_type == TYPE_DEV)
            return inode->i_fop->read(file, buf, count);

        if (file->f_pos >= inode->i_size)
            return 0;
    }

    ssize_t bytes = file->f_op->read(file, buf, count);
    if (bytes > 0)
        file->f_pos += bytes;
    return bytes;
}
SYSCALL_DECL3(read, int, fd, void*, buf, size_t, count)
{
    struct file *file;
    unsigned char *kbuf;
    ssize_t bytes;
    long err = -1;

#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "syscall read: Reading from fd %d\n", fd);
#endif

    file = get_file_handle(fd);
    if (IS_ERR(file)) {
#ifdef DEBUG_VFS
        klog(LOG_DEBUG, "syscall read: Invalid fd %d\n", fd);
#endif
        return PTR_ERR(file);
    }

    kbuf = kmalloc(count);
    if (!kbuf)
        return -ENOMEM;

    bytes = vfs_read(file, kbuf, count);
    if (bytes > 0)
        err = copy_to_user(buf, kbuf, bytes);

    kfree(kbuf);
    return err ? err : bytes;
}


ssize_t vfs_write(struct file *file, const void *buf, size_t count)
{
    if (file->f_dentry) {
        struct inode *inode = file->f_dentry->d_inode;
        if (inode->i_type == TYPE_DEV) {
            return inode->i_fop->write(file, buf, count);
        }
    }

    ssize_t bytes = file->f_op->write(file, buf, count);
    if (bytes > 0)
        file->f_pos += bytes;
    return bytes;
}
SYSCALL_DECL3(write, int, fd, const void*, buf, size_t, count)
{
    struct file *file;
    unsigned char *kbuf;
    ssize_t bytes;
    long err;

#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "syscall write: Writing to fd %d\n", fd);
#endif

    file = get_file_handle(fd);
    if (IS_ERR(file)) {
#ifdef DEBUG_VFS
        klog(LOG_DEBUG, "syscall write: Invalid fd %d\n", fd);
#endif
        return PTR_ERR(file);
    }
    kbuf = kmalloc(count);
    if (!kbuf)
        return -ENOMEM;

    err = copy_from_user(kbuf, buf, count);
    if (err) {
        kfree(kbuf);
        return err;
    }

    bytes = vfs_write(file, buf, count);
    kfree(kbuf);
    return bytes;
}

int vfs_close(struct file *file)
{
    klog(LOG_DEBUG, "vfs_close: file = %p, dentry = %p\n", file, file->f_dentry);
#ifdef DEBUG_VFS_FULL
    klog(LOG_DEBUG, "vfs_close: fdtable = %p, max = %d\n", current->fs.files.fdarray, current->fs.files.max);
    for (size_t i = 0; i < current->fs.files.max; i++) {
        klog(LOG_DEBUG, "table[%u] = %p\n", i, current->fs.files.fdarray[i]);
    }
#endif
    if (file->f_op->flush)
        file->f_op->flush(file);

    fput(file);

    return 0;
}
SYSCALL_DECL1(close, int, fd)
{
    struct file *file;
    long err;

    file = get_file_handle(fd);
    if (IS_ERR(file))
        return PTR_ERR(file);

#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "syscall close: Closing fd %d, file %p, dentry %p\n", fd, file, file->f_dentry);
#endif
    err = vfs_close(file);

    current->fs.files.fdarray[fd] = NULL;
    return err;
}


ssize_t vfs_getdents(struct file *file, struct dirent *dirp, unsigned int buf_size)
{
    struct inode *inode = file->f_dentry->d_inode;
    int dir_cnt;

    if (inode->i_type != TYPE_DIR) {
        klog(LOG_DEBUG, "VFS: getdents from non-directory %s\n",
            file->f_dentry->d_name);
        return -ENOTDIR;
    }
#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "VFS: Reading directory %s\n", file->f_dentry->d_name);
#endif
    dir_cnt = buf_size / sizeof(struct dirent);
    dir_cnt = file->f_op->readdir(file, dirp, dir_cnt);
#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "VFS: Read %d entries\n", dir_cnt);
#endif
    file->f_pos += dir_cnt;
    return dir_cnt > 0 ? dir_cnt * (int)sizeof(struct dirent) : dir_cnt;
}
SYSCALL_DECL3(getdents, unsigned int, fd, struct dirent*, dirp, size_t, buf_size)
{
    struct file *file = get_file_handle(fd);
    unsigned char *buf;
    ssize_t bytes;
#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "VFS: Getting directory entries for fd %d\n", fd);
#endif
    if (IS_ERR(file))
        return PTR_ERR(file);

    if (buf_size > 0x8000)
        return -EINVAL;

    buf = kzmalloc(buf_size);
    if (!buf)
        return -ENOMEM;

    bytes = vfs_getdents(file, (struct dirent *)buf, buf_size);
    long err = 0;
    if (bytes > 0)
        err = copy_to_user(dirp, buf, bytes);
    if (err < 0)
        bytes = err;

    kfree(buf);
    return bytes;
}

int vfs_mkdir(const char *path, umode_t mode)
{
    struct inode *parent;
    struct dentry *new_dentry;
    char *name = kmalloc(16);
    char dir_path[64];
    long err;

#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "VFS: Creating directory %s with mode %o\n", path, mode);
#endif
    get_dirname(dir_path, path, 64);
    get_basename(name, path, 16);

    struct dentry *parent_dentry = vfs_lookup(dir_path);
    if (IS_ERR(parent_dentry))
        return PTR_ERR(parent_dentry);

    parent = parent_dentry->d_inode;
    if (!parent)
        return -ENOENT;
    if (parent->i_type != TYPE_DIR) {
        klog(LOG_DEBUG, "VFS: Parent %s is not a directory\n", dir_path);
        return -ENOTDIR;
    }

    new_dentry = alloc_dentry(parent_dentry, name);
    if (IS_ERR(new_dentry))
        return PTR_ERR(new_dentry);

    err = parent->i_op->mkdir(parent, new_dentry, mode);
    if (err < 0) {
        destroy_dentry(new_dentry);
        return err;
    }

    dget(new_dentry);
    dcache_add(new_dentry);
    klog(LOG_DEBUG, "VFS: Created directory %s\n", new_dentry->d_name);
    return 0;
}
SYSCALL_DECL2(mkdir, const char*, path, umode_t, mode)
{
    char *path_buf = kmalloc(256);
    long err = -EFAULT;

    err = strncpy_from_user(path_buf, path, 255);
    if (err == 0) {
        err = -EINVAL;
        goto error;
    } else if (err < 0) {
        goto error;
    }
    err = vfs_mkdir(path_buf, mode);
error:
    kfree(path_buf);
    return err;
}

int vfs_create(const char *path, umode_t mode)
{
    char dirname[64];
    char *basename = kmalloc(16);
    struct inode *parent_inode;
    struct dentry *parent;

    get_dirname(dirname, path, 64);
    get_basename(basename, path, 16);

    parent = lookup_path(dirname);
    if (IS_ERR(parent)) {
        klog(LOG_DEBUG, "Failed to find parent %s\n", dirname);
        return PTR_ERR(parent);
    }
    parent_inode = parent->d_inode;
    if (!parent_inode) {
        klog(LOG_DEBUG, "Parent inode not found\n");
        return -ENOENT;
    }

    struct dentry *new_dentry = alloc_dentry(parent, basename);
    dget(new_dentry);
    dcache_add(new_dentry);
#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "Creating %s\n", new_dentry->d_name);
#endif
    return parent_inode->i_op->create(parent_inode, new_dentry, mode);
}
SYSCALL_DECL2(create, const char*, path, umode_t, mode)
{
    char *path_buf = kmalloc(256);
    long err = -EFAULT;

    err = strncpy_from_user(path_buf, path, 255);
    if (err >= 0)
        err = vfs_create(path, mode);
    kfree(path_buf);
    return err;
}

int mknod(const char *pathname, int mode, dev_t dev)
{
    return -ENOSYS;
}
SYSCALL_DECL3(mknod, const char*, pathname, int, mode, dev_t, dev)
{
    return mknod(pathname, mode, dev);
}

int vfs_dup(int oldfd, int newfd)
{
    struct file *f = get_file_handle(oldfd);
    if (IS_ERR(f))
        return PTR_ERR(f);
    struct fdtable *files = &current->fs.files;
#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "vfs_dup: file %p, dentry %p, oldfd %d, newfd %d\n", f, f->f_dentry, oldfd, newfd);
#endif
    if (newfd < 0 || newfd >= (int)files->max) {
        klog(LOG_ERROR, "VFS: vfs_dup invalid newfd %d\n", newfd);
        return -EBADF;
    }
    if (newfd < (int)files->max && current->fs.files.fdarray[newfd]) {
        vfs_close(current->fs.files.fdarray[newfd]);
    }
    fget(f);
    current->fs.files.fdarray[newfd] = f;
#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "VFS: Duplicated fd %d to %d\n", oldfd, newfd);
#endif
    return newfd;
}

SYSCALL_DECL1(dup, int, oldfd)
{
    int newfd = get_fd();
    if (newfd < 0) {
        return newfd; // -EMFILE
    }

    return vfs_dup(oldfd, newfd);
}

SYSCALL_DECL2(dup2, int, oldfd, int, newfd)
{
    return vfs_dup(oldfd, newfd);
}

static char * build_absolute_path(struct dentry *d)
{
    if (!d) return ERR_PTR(-EINVAL);

    char tmp[1024] = {0};
    char *buf = kmalloc(1024);
    if (!buf) return ERR_PTR(-ENOMEM);
    buf[0] = '\0';

    if (d == root_dentry) {
        strcpy(buf, "/");
        return buf;
    } else {
        strcpy(buf, d->d_name);
    }

    struct dentry *cur = d->d_parent;
    while (cur) {
#ifdef DEBUG_VFS
        klog(LOG_DEBUG, "build_absolute_path: cur = %p, name = %s, buf = %s\n", cur, cur->d_name, buf);
#endif
        if (cur == current->fs.root_d) {
            // Reached root
            snprintf(tmp, 64, "%c%s", '/', buf);
            strcpy(buf, tmp);
            break;
        } else {
            snprintf(tmp, 64, "%s/%s", cur->d_name, buf);
            strcpy(buf, tmp);
        }
        cur = cur->d_parent;
    }
    return buf;
}

SYSCALL_DECL2(getcwd, char*, buf, size_t, size)
{
    if (!buf || size == 0 || size > 4096)
        return -EINVAL;

    struct dentry *cwd = current->fs.cwd_d;
    char *path = build_absolute_path(cwd);
    if (IS_ERR(path))
        return PTR_ERR(path);

    int len = strlen(path);
    if (len >= (int)size) {
        kfree(path);
        return -ERANGE;
    }

    len = copy_to_user(buf, path, len + 1);
    kfree(path);
    return len < 0 ? len : 0;
}
