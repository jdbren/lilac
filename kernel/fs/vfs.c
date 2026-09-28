// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/fs.h>

#include <lilac/lilac.h>
#include <lilac/boot.h>
#include <lilac/libc.h>
#include <lilac/syscall.h>
#include <lilac/device.h>
#include <lilac/sched.h>
#include <lilac/timer.h>
#include <lilac/uaccess.h>
#include <drivers/blkdev.h>
#include <fs/fcntl.h>
#include <fs/fat32.h>
#include <fs/tmpfs.h>
#include <fs/ext2.h>

#include "utils.h"

// #define DEBUG_VFS 1

static fs_init_func_t init_ops[4] = {
    fat32_init, ext2_init, tmpfs_init, NULL
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


static void root_init(struct block_device *bdev, enum fs_type type)
{
    if (!bdev)
        kerror("Invalid block device for root\n");
    if (type <= FSTYPE_ERROR || type >= MAX_FS_TYPES)
        kerror("Invalid filesystem type for root\n");

    struct super_block *sb = alloc_sb(bdev);
    if (!sb)
        kerror("Failed to allocate superblock\n");
    struct vfsmount *root_disk = get_empty_vfsmount(type);
    if (!root_disk)
        kerror("Failed to get empty vfsmount for root\n");

    root_disk->mnt_sb = sb;

    root_dentry = root_disk->init_fs(bdev, sb);
    if (!root_dentry)
        kerror("Failed to initialize root dentry\n");
    root_disk->mnt_root = root_dentry;

    root_dentry->d_parent = NULL;
    root_dentry->d_count = 1;
    root_dentry->d_name.data = strdup("/");
    root_dentry->d_name.len = 1;
    root_dentry->d_mount = root_disk;
    INIT_HLIST_HEAD(&root_dentry->d_children);
    INIT_HLIST_NODE(&root_dentry->d_sib);

    klog(LOG_INFO, "%s mounted on /\n", bdev->name);
}

static bool get_root_uuid(const char *boot_args, char root_uuid[37])
{
    const char *uuid_start = strstr(boot_args, "root=");
    if (!uuid_start)
        return false;
    uuid_start += 5; // Skip "root="

    const char *end = strchr(uuid_start, ' ');
    size_t len = end ? (size_t)(end - uuid_start) : strnlen(uuid_start, 36);
    memcpy(root_uuid, uuid_start, len);
    root_uuid[len] = '\0';
    return true;
}

static bool get_root_fstype(const char *boot_args, char root_fstype[16])
{
    const char *fstype_start = strstr(boot_args, "rootfstype=");
    if (!fstype_start)
        return false;
    fstype_start += 11; // Skip "rootfstype="

    const char *end = strchr(fstype_start, ' ');
    size_t len = end ? (size_t)(end - fstype_start) : strnlen(fstype_start, 15);
    memcpy(root_fstype, fstype_start, len);
    root_fstype[len] = '\0';
    return true;
}

void fs_init(void)
{
    struct block_device *bdev;
    char root_uuid[37];
    char root_fstype[16];
    if (!get_root_uuid(boot_info.mbd.cmdline, root_uuid))
        kerror("Failed to get root UUID from boot args\n");
    if (!get_root_fstype(boot_info.mbd.cmdline, root_fstype))
        kerror("Failed to get root filesystem type from boot args\n");

    if (scan_partitions(NULL))
        kerror("Partition scan failed\n");
    bdev = get_bdev_by_uuid(root_uuid);
    if (!bdev)
        kerror("Failed to get root block device\n");

    root_init(bdev, str_to_fstype(root_fstype));

    struct block_device *tmp_bdev = kzmalloc(sizeof(struct block_device));
    struct block_device *dev_bdev = kzmalloc(sizeof(struct block_device));
    if (!tmp_bdev || !dev_bdev)
        kerror("Failed to allocate block devices\n");
    strcpy(tmp_bdev->name, "tmpfs");
    strcpy(dev_bdev->name, "devfs");

    vfs_mount(tmp_bdev, "/tmp", "tmpfs", 0, NULL);
    vfs_mount(dev_bdev, "/dev", "tmpfs", 0, NULL);

#ifdef DEBUG
    struct block_device *bd = get_bdev_by_uuid("5376933F-2B06-489B-843D-3535E656468E");
    if (bd)
        vfs_mount(bd, "/mnt", "ext2", 0, NULL);
#endif

    kstatus(STATUS_OK, "Filesystem initialized\n");
}


#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

off_t vfs_lseek_unlocked(struct file *file, off_t offset, int whence)
{
    struct inode *inode = file->f_dentry->d_inode;

    switch (whence) {
    case SEEK_CUR:
        if (offset == 0)
            return file->f_pos;
        offset += file->f_pos;
        break;
    case SEEK_END:
        acquire_lock(&inode->i_lock);
        offset += inode->i_size;
        release_lock(&inode->i_lock);
        break;
    case SEEK_SET:
        break;
    default:
        return -EINVAL;
    }

    if (offset < 0)
        return -EINVAL;

    if (offset != file->f_pos) {
        file->f_pos = offset;
    }

    return offset;
}

off_t vfs_lseek(struct file *file, off_t offset, int whence)
{
    if (file->f_op->lseek)
        return file->f_op->lseek(file, offset, whence);

    mutex_lock(&file->f_pos_lock);
    off_t ret = vfs_lseek_unlocked(file, offset, whence);
    mutex_unlock(&file->f_pos_lock);
    return ret;
}

static struct dentry * get_parent_dentry(const char *path, struct dentry *start);

static int vfs_rename(const char *oldpath, const char *newpath)
{
    char name[NAME_MAX];
    struct dentry *new_parent = NULL, *new_d = NULL;
    int err;
    struct dentry *old_d = vfs_lookup_flags(oldpath, 0);
    if (IS_ERR(old_d))
        return PTR_ERR(old_d);
    if (!old_d->d_inode) {
        err = -ENOENT;
        goto out;
    }
    if (old_d == root_dentry || !old_d->d_parent || old_d->d_mount) {
        err = -EBUSY;
        goto out;
    }

    new_parent = get_parent_dentry(newpath, NULL);
    if (IS_ERR(new_parent)) {
        err = PTR_ERR(new_parent);
        new_parent = NULL;
        goto out;
    }
    if (!new_parent->d_inode) {
        err = -ENOENT;
        goto out;
    }
    if (!S_ISDIR(new_parent->d_inode->i_mode)) {
        err = -ENOTDIR;
        goto out;
    }
    err = get_basename(name, newpath, NAME_MAX);
    if (err)
        goto out;
    new_d = lookup_path_from_flags(new_parent, name, 0);
    if (IS_ERR(new_d)) {
        err = PTR_ERR(new_d);
        new_d = NULL;
        goto out;
    }

    struct inode *old_dir = old_d->d_parent->d_inode;
    struct inode *new_dir = new_parent->d_inode;
    struct inode *inode = old_d->d_inode;
    err = 0;
    if (old_dir->i_sb != new_dir->i_sb) {
        err = -EXDEV;
        goto out;
    }
    if (new_d->d_inode == inode)
        goto out;
    if (new_d->d_mount) {
        err = -EBUSY;
        goto out;
    }
    if (new_d->d_inode) {
        if (S_ISDIR(inode->i_mode) && !S_ISDIR(new_d->d_inode->i_mode)) {
            err = -ENOTDIR;
            goto out;
        }
        if (!S_ISDIR(inode->i_mode) && S_ISDIR(new_d->d_inode->i_mode)) {
            err = -EISDIR;
            goto out;
        }
    }
    // directory can't move inside itself
    for (struct dentry *d = new_parent; d; d = d->d_parent) {
        if (d == old_d) {
            err = -EINVAL;
            goto out;
        }
    }
    if (!old_dir->i_op->rename) {
        err = -EPERM;
        goto out;
    }

    err = old_dir->i_op->rename(old_dir, old_d, new_dir, new_d);
    if (err)
        goto out;

    // new_d's name now belongs to old_d: retire it and move old_d there
    if (new_d->d_inode && S_ISDIR(new_d->d_inode->i_mode))
        d_prune_negative(new_d);
    d_drop(new_d);
    err = d_move(old_d, new_parent, name);
    if (err)
        d_drop(old_d);
out:
    if (new_d)
        dput(new_d);
    if (new_parent)
        dput(new_parent);
    dput(old_d);
    return err;
}

SYSCALL_DECL2(rename, const char*, oldpath, const char*, newpath)
{
    char *old_buf = get_user_path(oldpath);
    if (IS_ERR(old_buf))
        return PTR_ERR(old_buf);
    char *new_buf = get_user_path(newpath);
    if (IS_ERR(new_buf)) {
        kfree(old_buf);
        return PTR_ERR(new_buf);
    }
    long err = vfs_rename(old_buf, new_buf);
    kfree(old_buf);
    kfree(new_buf);
    return err;
}

long vfs_ftruncate(struct file *f, loff_t length)
{
    struct inode *inode = f->f_inode;
    if (length < 0)
        return -EINVAL;
    if (!inode || S_ISDIR(inode->i_mode))
        return -EISDIR;
    if (!S_ISREG(inode->i_mode))
        return -EINVAL;
    if ((f->f_mode & O_ACCMODE) == O_RDONLY)
        return -EBADF;
    if (!inode->i_op || !inode->i_op->truncate)
        return -EROFS;

    if (length <= inode->i_size)
        return inode->i_op->truncate(inode, length);

    // Grow by writing zeros so the filesystem allocates and clears the space
    const size_t chunk = 4096;
    void *zero = kzmalloc(chunk);
    if (!zero)
        return -ENOMEM;
    long err = 0;
    for (loff_t pos = inode->i_size; pos < length; ) {
        size_t n = MIN((loff_t)chunk, length - pos);
        ssize_t w = vfs_write_at(f, zero, n, pos);
        if (w <= 0) {
            err = w < 0 ? w : -EIO;
            break;
        }
        pos += w;
    }
    kfree(zero);
    return err;
}

SYSCALL_DECL2(ftruncate, int, fd, off_t, length)
{
    struct file *file = get_file_handle(fd);
    if (IS_ERR(file))
        return PTR_ERR(file);
    return vfs_ftruncate(file, length);
}

SYSCALL_DECL2(truncate, const char*, path, off_t, length)
{
    char *path_buf = get_user_path(path);
    if (IS_ERR(path_buf))
        return PTR_ERR(path_buf);
    struct file *f = vfs_open(path_buf, O_WRONLY, 0);
    kfree(path_buf);
    if (IS_ERR(f))
        return PTR_ERR(f);
    long err = vfs_ftruncate(f, length);
    vfs_close(f);
    return err;
}

// Writes go straight to the block device; nothing is cached to flush
// TODO: cache writes
SYSCALL_DECL1(fsync, int, fd)
{
    struct file *file = get_file_handle(fd);
    return IS_ERR(file) ? PTR_ERR(file) : 0;
}

SYSCALL_DECL3(lseek, int, fd, off_t, offset, int, whence)
{
    struct file *file = get_file_handle(fd);
    if (IS_ERR(file))
        return PTR_ERR(file);
    if (file->f_inode && S_ISFIFO(file->f_inode->i_mode))
        return -ESPIPE;
    return vfs_lseek(file, offset, whence);
}

struct dentry * vfs_lookup_flags(const char *path, int follow_final)
{
    struct dentry *start = root_dentry;
    if (*path == '\0')
        return ERR_PTR(-ENOENT);

    if (path[0] != '/')
        start = current->fs->cwd_d;

    return lookup_path_from_flags(start, path, follow_final);
}

struct dentry * vfs_lookup(const char *path)
{
    return vfs_lookup_flags(path, 1);
}

static int create_file_at(struct dentry *new_d, umode_t mode)
{
    klog(LOG_DEBUG, "Creating file %s with mode %o\n", new_d->d_name.data, mode);
    struct inode *parent_inode = new_d->d_parent->d_inode;
    if (!parent_inode->i_op->create)
        return -EROFS;
    return parent_inode->i_op->create(parent_inode, new_d, mode);
}

static struct file *vfs_open_dentry(struct dentry *dentry, const char *path,
    int flags, int mode);

struct file *vfs_open(const char *path, int flags, int mode)
{
    klog(LOG_DEBUG, "VFS: Opening %s, flags = %x, mode = %x\n", path, flags, mode);
    struct dentry *dentry = vfs_lookup_flags(path, !(flags & O_NOFOLLOW));
    if (IS_ERR(dentry))
        return ERR_CAST(dentry);
    // the file takes its own reference
    struct file *file = vfs_open_dentry(dentry, path, flags, mode);
    dput(dentry);
    return file;
}

static struct file *vfs_open_dentry(struct dentry *dentry, const char *path,
    int flags, int mode)
{
    struct inode *inode;
    struct file *new_file;
    long err;
    inode = dentry->d_inode;
    if (inode && S_ISLNK(inode->i_mode))
        return ERR_PTR(-ELOOP);
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
    } else if ((flags & O_CREAT) && (flags & O_EXCL)) {
        klog(LOG_DEBUG, "VFS: File %s already exists\n", path);
        return ERR_PTR(-EEXIST);
    }

    if (S_ISDIR(inode->i_mode) && (flags & O_ACCMODE) != O_RDONLY) {
        klog(LOG_DEBUG, "VFS: Cannot open directory %s with write access\n", path);
        return ERR_PTR(-EISDIR);
    } else if (!S_ISDIR(inode->i_mode) && flags & O_DIRECTORY) {
        klog(LOG_DEBUG, "VFS: Not a directory: %s\n", path);
        return ERR_PTR(-ENOTDIR);
    }

    new_file = alloc_file(dentry);
    if (IS_ERR_OR_NULL(new_file))
        return ERR_PTR(-ENOMEM);

    if (inode->i_op->open) {
        if ((err = inode->i_op->open(inode, new_file)) < 0) {
            fput(new_file);
            return ERR_PTR(err);
        }
    } else {
        klog(LOG_DEBUG, "VFS: No open operation for inode %p\n", inode);
        new_file->f_op = inode->i_fop;
    }

    new_file->f_mode = flags & ~(O_CREAT|O_EXCL|O_NOCTTY|O_TRUNC|O_CLOEXEC);

    if ((flags & O_TRUNC) && S_ISREG(inode->i_mode) &&
            (flags & O_ACCMODE) != O_RDONLY && inode->i_size > 0) {
        err = vfs_ftruncate(new_file, 0);
        if (err < 0) {
            vfs_close(new_file);
            return ERR_PTR(err);
        }
    }

    return new_file;
}
SYSCALL_DECL3(open, const char*, path, int, flags, int, mode)
{
    char *path_buf;
    struct file *f;
    long fd;

    path_buf = get_user_path(path);
    if (IS_ERR(path_buf))
        return PTR_ERR(path_buf);

    f = vfs_open(path_buf, flags, mode);
    if (IS_ERR(f)) {
        klog(LOG_DEBUG, "Failed to open %s\n", path_buf);
        fd = PTR_ERR(f);
        goto error;
    }

    fd = get_next_fd(current->files, f);
    if (fd < 0) {
        klog(LOG_DEBUG, "No available file descriptors for %s\n", path_buf);
        fput(f);
        goto error;
    }

    if (flags & O_CLOEXEC)
        set_cloexec(current->files, fd);

#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "VFS: Opened file %s with fd %d\n", path_buf, fd);
#endif
error:
    kfree(path_buf);
    return fd;
}

ssize_t vfs_read_at(struct file *file, void *buf, size_t count, unsigned long pos)
{
    if (file->f_dentry) {
        struct inode *inode = file->f_dentry->d_inode;
        if (S_ISDIR(inode->i_mode))
            return -EISDIR;

        if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
            return inode->i_fop->read(file, buf, count);

        if (pos >= inode->i_size)
            return 0;
    }

    // f_op->read reads at f_pos, so hold the lock across the swap; the file
    // may be shared (e.g. a mapping inherited across fork)
    mutex_lock(&file->f_pos_lock);
    unsigned long old_pos = file->f_pos;
    file->f_pos = pos;
    ssize_t bytes = file->f_op->read(file, buf, count);
    file->f_pos = old_pos;
    mutex_unlock(&file->f_pos_lock);

    return bytes;
}

ssize_t vfs_read(struct file *file, void *buf, size_t count)
{
    if (file->f_dentry) {
        struct inode *inode = file->f_dentry->d_inode;
        if (S_ISDIR(inode->i_mode))
            return -EISDIR;

        if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
            return inode->i_fop->read(file, buf, count);

        if (file->f_pos >= inode->i_size)
            return 0;
    }

#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "vfs_read: Reading %lu bytes from file %s at pos %lu\n",
        count, file->f_dentry ? file->f_dentry->d_name : "unknown", file->f_pos);
#endif

    mutex_lock(&file->f_pos_lock);
    ssize_t bytes = file->f_op->read(file, buf, count);
    if (bytes > 0)
        file->f_pos += bytes;
    mutex_unlock(&file->f_pos_lock);
#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "vfs_read: Read %ld bytes from file %s at pos %lu\n",
        bytes, file->f_dentry ? file->f_dentry->d_name : "unknown", file->f_pos);
#endif
    return bytes;
}
SYSCALL_DECL3(read, int, fd, void*, buf, size_t, count)
{
    struct file *file;
    unsigned char *kbuf;
    ssize_t bytes;

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

    if ((file->f_mode & O_ACCMODE) == O_WRONLY)
        return -EBADF;

    if (count == 0)
        return 0;

    kbuf = kmalloc(count);
    if (!kbuf)
        return -ENOMEM;

    bytes = vfs_read(file, kbuf, count);
    if (bytes > 0 && copy_to_user(buf, kbuf, bytes))
        bytes = -EFAULT;

    kfree(kbuf);
    return bytes;
}


ssize_t vfs_write_at(struct file *file, const void *buf, size_t count, unsigned long pos)
{
    if (file->f_dentry) {
        struct inode *inode = file->f_dentry->d_inode;
        if (S_ISDIR(inode->i_mode))
            return -EISDIR;
        if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode))
            return inode->i_fop->write(file, buf, count);
    }

    mutex_lock(&file->f_pos_lock);
    unsigned long old_pos = file->f_pos;
    file->f_pos = pos;
    ssize_t bytes = file->f_op->write(file, buf, count);
    file->f_pos = old_pos;
    mutex_unlock(&file->f_pos_lock);

    return bytes;
}


ssize_t vfs_write(struct file *file, const void *buf, size_t count)
{
    if (file->f_dentry) {
        struct inode *inode = file->f_dentry->d_inode;
        if (S_ISDIR(inode->i_mode))
            return -EISDIR;
        if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode)) {
            return inode->i_fop->write(file, buf, count);
        }
    }

    mutex_lock(&file->f_pos_lock);
    if ((file->f_mode & O_APPEND) && file->f_inode)
        file->f_pos = file->f_inode->i_size;
    ssize_t bytes = file->f_op->write(file, buf, count);
    if (bytes > 0)
        file->f_pos += bytes;
    mutex_unlock(&file->f_pos_lock);
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

    if ((file->f_mode & O_ACCMODE) == O_RDONLY)
        return -EBADF;

    if (count == 0)
        return 0;

    kbuf = kmalloc(count);
    if (!kbuf)
        return -ENOMEM;

    err = copy_from_user(kbuf, buf, count);
    if (err) {
        kfree(kbuf);
        return err;
    }

    bytes = vfs_write(file, kbuf, count);
    kfree(kbuf);
    return bytes;
}

int vfs_close(struct file *file)
{
    klog(LOG_DEBUG, "vfs_close: file = %p, dentry = %p\n", file, file->f_dentry);
#ifdef DEBUG_VFS_FULL
    klog(LOG_DEBUG, "vfs_close: fdtable = %p, max = %d\n",
        current->files->fdarray, current->files->max);
    for (size_t i = 0; i < current->files->max; i++) {
        klog(LOG_DEBUG, "table[%u] = %p\n", i, current->files->fdarray[i]);
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
    klog(LOG_DEBUG, "syscall close: Closing fd %d, file %p, dentry %p\n",
        fd, file, file->f_dentry);
#endif
    err = vfs_close(file);
    acquire_lock(&current->files->lock);
    current->files->fdarray[fd] = NULL;
    release_lock(&current->files->lock);
    return err;
}


ssize_t vfs_getdents(struct file *file, struct dirent *dirp, int buf_size)
{
    struct inode *inode = file->f_dentry->d_inode;
    int dir_cnt;

    if (!S_ISDIR(inode->i_mode)) {
        klog(LOG_DEBUG, "VFS: getdents from non-directory %s\n",
            file->f_dentry->d_name);
        return -ENOTDIR;
    }
#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "VFS: Reading directory %s\n", file->f_dentry->d_name.data);
#endif
    dir_cnt = buf_size / sizeof(struct dirent);
    dir_cnt = file->f_op->readdir(file, dirp, dir_cnt);
#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "VFS: Read %d entries\n", dir_cnt);
#endif
    return dir_cnt > 0 ? dir_cnt * (int)sizeof(struct dirent) : dir_cnt;
}

SYSCALL_DECL3(getdents, int, fd, struct dirent*, dirp, int, buf_size)
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

static struct dentry * get_parent_dentry(const char *path, struct dentry *start)
{
    char *dir_path = kmalloc(PATH_MAX);
    if (!dir_path)
        return ERR_PTR(-ENOMEM);

    get_dirname(dir_path, path, PATH_MAX);

    struct dentry *parent_dentry = (start != NULL) ?
        lookup_path_from(start, dir_path) : vfs_lookup(dir_path);
    kfree(dir_path);
    return parent_dentry;
}

static int vfs_do_mkdir(struct dentry * parent_d, const char *name, umode_t mode)
{
    struct inode *parent_i = parent_d->d_inode;
    if (!parent_i)
        return -ENOENT;
    if (!S_ISDIR(parent_i->i_mode))
        return -ENOTDIR;
    if (parent_i->i_op->mkdir == NULL)
        return -EPERM;

    // Looks up (and caches) the name in the parent directory
    struct dentry *new_dentry = lookup_path_from_flags(parent_d, name, 0);
    if (IS_ERR(new_dentry))
        return PTR_ERR(new_dentry);
    int err = -EEXIST;
    if (!new_dentry->d_inode)
        err = parent_i->i_op->mkdir(parent_i, new_dentry, mode);
    dput(new_dentry);
    return err;
}

int vfs_mkdir(const char *path, umode_t mode)
{
    int err = 0;
    char name[NAME_MAX];

#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "VFS: Creating directory %s with mode %o\n", path, mode);
#endif

    struct dentry *parent_d = get_parent_dentry(path, NULL);
    if (IS_ERR(parent_d))
        return PTR_ERR(parent_d);

    if ((err = get_basename(name, path, NAME_MAX)))
        goto error;

    err = vfs_do_mkdir(parent_d, name, mode);
    if (err < 0)
        goto error;

    klog(LOG_DEBUG, "VFS: Created directory %s\n", path);
error:
    dput(parent_d);
    return err;
}

SYSCALL_DECL2(mkdir, const char*, path, umode_t, mode)
{
    char *path_buf;
    long err;

    path_buf = get_user_path(path);
    if (IS_ERR(path_buf))
        return PTR_ERR(path_buf);

    err = vfs_mkdir(path_buf, mode);
    kfree(path_buf);
    return err;
}

int vfs_mkdirat(struct file *dirf, const char *path, umode_t mode)
{
    int err = 0;
    char name[NAME_MAX];

    struct dentry *parent_d = get_parent_dentry(path, dirf->f_dentry);
    if (IS_ERR(parent_d))
        return PTR_ERR(parent_d);

    get_basename(name, path, NAME_MAX);

    err = vfs_do_mkdir(parent_d, name, mode);
    if (err < 0)
        goto error;
    klog(LOG_DEBUG, "VFS: Created directory %s\n", path);
error:
    dput(parent_d);
    return err;
}

SYSCALL_DECL3(mkdirat, int, dirfd, const char*, path, umode_t, mode)
{
    char *path_buf;
    long err;

    path_buf = get_user_path(path);
    if (IS_ERR(path_buf))
        return PTR_ERR(path_buf);

    if (is_absolute_path(path_buf)) {
        err = vfs_mkdir(path_buf, mode);
    } else {
        struct file *dirf = get_file_handle(dirfd);
        if (IS_ERR(dirf)) {
            kfree(path_buf);
            return PTR_ERR(dirf);
        }
        err = vfs_mkdirat(dirf, path_buf, mode);
    }

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
        dput(parent);
        return -ENOENT;
    }

    struct dentry *new_dentry = alloc_dentry(parent, basename);
    kfree(basename);
    if (IS_ERR(new_dentry)) {
        dput(parent);
        return PTR_ERR(new_dentry);
    }
    mutex_lock(&parent->d_lock);
    dcache_add(new_dentry);
    mutex_unlock(&parent->d_lock);
#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "Creating %s\n", new_dentry->d_name.data);
#endif
    int err = parent_inode->i_op->create(parent_inode, new_dentry, mode);
    dput(new_dentry);
    dput(parent);
    return err;
}
SYSCALL_DECL2(create, const char*, path, umode_t, mode)
{
    char *path_buf;
    long err;

    path_buf = get_user_path(path);
    if (IS_ERR(path_buf))
        return PTR_ERR(path_buf);
    err = vfs_create(path_buf, mode);
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
    if (newfd < 0 || newfd >= 1024) {
        klog(LOG_ERROR, "VFS: vfs_dup invalid newfd %d\n", newfd);
        return -EBADF;
    }

    if (oldfd == newfd)
        return newfd;

    struct file *f = get_file_handle(oldfd);
    if (IS_ERR(f))
        return PTR_ERR(f);

    fget(f);

#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "vfs_dup: file %p, dentry %p, oldfd %d, newfd %d\n",
        f, f->f_dentry, oldfd, newfd);
#endif

    int ret;
    if ((ret = get_fd_exact_replace(current->files, newfd, f)) < 0) {
        fput(f);
        klog(LOG_ERROR, "VFS: vfs_dup failed to set fd %d\n", newfd);
        return ret;
    }

#ifdef DEBUG_VFS
    klog(LOG_DEBUG, "VFS: Duplicated fd %d to %d\n", oldfd, newfd);
#endif
    return ret;
}

int vfs_dupf(int fd)
{
    struct file *f = get_file_handle(fd);
    if (IS_ERR(f))
        return PTR_ERR(f);
    fget(f);
    int new_fd = get_next_fd(current->files, f);
    if (new_fd < 0) {
        klog(LOG_ERROR, "VFS: vfs_dupf failed to get new fd\n");
        fput(f);
    }
    return new_fd;
}

SYSCALL_DECL1(dup, int, oldfd)
{
    return vfs_dupf(oldfd);
}

SYSCALL_DECL2(dup2, int, oldfd, int, newfd)
{
    return vfs_dup(oldfd, newfd);
}

SYSCALL_DECL2(getcwd, char*, buf, size_t, size)
{
    if (!buf || size == 0 || size > 4096)
        return -EINVAL;

    struct dentry *cwd = current->fs->cwd_d;
    char *path = build_absolute_path(cwd);
    if (IS_ERR(path))
        return PTR_ERR(path);

    int len = strlen(path);
    if (len >= (int)size) {
        kfree(path);
        return -ERANGE;
    }

    int ret = copy_to_user(buf, path, len + 1);
    kfree(path);
    return ret != 0 ? ret : len+1;
}

int vfs_rmdir(const char *path)
{
    struct dentry *dentry = vfs_lookup_flags(path, 0);
    if (IS_ERR(dentry))
        return PTR_ERR(dentry);

    struct inode *dir = dentry->d_parent ? dentry->d_parent->d_inode : NULL;
    int err;
    if (!dentry->d_inode)
        err = -ENOENT;
    else if (!S_ISDIR(dentry->d_inode->i_mode))
        err = -ENOTDIR;
    else if (!dir || dentry->d_mount)
        err = -EBUSY;
    else if (dir->i_op->rmdir == NULL)
        err = -EPERM;
    else
        err = dir->i_op->rmdir(dir, dentry);
    if (!err) {
        d_prune_negative(dentry);
        d_drop(dentry);
    }
    dput(dentry);
    return err;
}

SYSCALL_DECL1(rmdir, const char*, path)
{
    char *path_buf;
    long err;

    path_buf = get_user_path(path);
    if (IS_ERR(path_buf))
        return PTR_ERR(path_buf);

    err = vfs_rmdir(path_buf);
    kfree(path_buf);
    return err;
}

int vfs_unlink(const char *path)
{
    struct dentry *dentry = vfs_lookup_flags(path, 0);
    if (IS_ERR(dentry))
        return PTR_ERR(dentry);

    struct inode *dir = dentry->d_parent ? dentry->d_parent->d_inode : NULL;
    struct inode *victim_i = dentry->d_inode;
    int err;
    if (!victim_i)
        err = -ENOENT;
    else if (S_ISDIR(victim_i->i_mode))
        err = -EISDIR;
    else if (!dir)
        err = -EBUSY;
    else if (!dir->i_op->unlink)
        err = -EPERM;
    else
        err = dir->i_op->unlink(dir, dentry);
    if (!err)
        d_drop(dentry);
    dput(dentry);
    return err;
}

SYSCALL_DECL1(unlink, const char*, path)
{
    char *path_buf;
    long err;

    path_buf = get_user_path(path);
    if (IS_ERR(path_buf))
        return PTR_ERR(path_buf);

    err = vfs_unlink(path_buf);
    kfree(path_buf);
    return err;
}

int vfs_link(const char *oldpath, const char *newpath)
{
    struct dentry *old_dentry = vfs_lookup(oldpath);
    if (IS_ERR(old_dentry))
        return PTR_ERR(old_dentry);

    struct inode *old_inode = old_dentry->d_inode;
    struct dentry *new_dentry = NULL;
    struct inode *dir;
    int err;
    if (!old_inode) {
        err = -ENOENT;
        goto out;
    }
    if (!S_ISREG(old_inode->i_mode)) { // only link regular files currently
        err = -EPERM;
        goto out;
    }

    new_dentry = vfs_lookup_flags(newpath, 0);
    if (IS_ERR(new_dentry)) {
        err = PTR_ERR(new_dentry);
        new_dentry = NULL;
        goto out;
    }
    dir = new_dentry->d_parent ? new_dentry->d_parent->d_inode : NULL;
    if (new_dentry->d_inode)
        err = -EEXIST;
    else if (!dir)
        err = -ENOENT;
    else if (!dir->i_op->link)
        err = -EPERM;
    else
        err = dir->i_op->link(old_dentry, dir, new_dentry);
out:
    if (new_dentry)
        dput(new_dentry);
    dput(old_dentry);
    return err;
}

SYSCALL_DECL2(link, const char*, oldpath, const char*, newpath)
{
    char *oldpath_buf;
    char *newpath_buf;
    long err;

    oldpath_buf = get_user_path(oldpath);
    if (IS_ERR(oldpath_buf))
        return PTR_ERR(oldpath_buf);

    newpath_buf = get_user_path(newpath);
    if (IS_ERR(newpath_buf)) {
        kfree(oldpath_buf);
        return PTR_ERR(newpath_buf);
    }

    err = vfs_link(oldpath_buf, newpath_buf);
    kfree(oldpath_buf);
    kfree(newpath_buf);
    return err;
}

int vfs_symlink(const char *target, const char *linkpath)
{
    struct dentry *link_dentry = vfs_lookup_flags(linkpath, 0);
    if (IS_ERR(link_dentry))
        return PTR_ERR(link_dentry);

    struct inode *dir = link_dentry->d_parent ? link_dentry->d_parent->d_inode : NULL;
    int err;
    if (link_dentry->d_inode)
        err = -EEXIST;
    else if (!dir)
        err = -ENOENT;
    else if (!dir->i_op->symlink)
        err = -EPERM;
    else
        err = dir->i_op->symlink(dir, link_dentry, target);
    dput(link_dentry);
    return err;
}

SYSCALL_DECL2(symlink, const char*, target, const char*, linkpath)
{
    char *target_buf;
    char *linkpath_buf;
    long err;

    target_buf = get_user_path(target);
    if (IS_ERR(target_buf))
        return PTR_ERR(target_buf);

    linkpath_buf = get_user_path(linkpath);
    if (IS_ERR(linkpath_buf)) {
        kfree(target_buf);
        return PTR_ERR(linkpath_buf);
    }

    err = vfs_symlink(target_buf, linkpath_buf);
    kfree(target_buf);
    kfree(linkpath_buf);
    return err;
}

SYSCALL_DECL3(readlink, const char*, path, char *, buf, int, bufsize)
{
    struct dentry *dentry = NULL;
    struct inode *inode;
    char *path_buf;
    long err;

    if (bufsize <= 0)
        return -EINVAL;
    if (!access_ok(buf, bufsize))
        return -EFAULT;

    path_buf = get_user_path(path);
    if (IS_ERR(path_buf))
        return PTR_ERR(path_buf);

    dentry = vfs_lookup_flags(path_buf, 0);
    if (IS_ERR(dentry)) {
        err = PTR_ERR(dentry);
        goto error;
    }

    if (!dentry->d_inode) {
        err = -ENOENT;
        goto error;
    }

    inode = dentry->d_inode;
    if (!S_ISLNK(inode->i_mode)) {
        err = -EINVAL;
        goto error;
    }

    if (inode->i_op->readlink) {
        err = inode->i_op->readlink(dentry, buf, bufsize);
    } else if (inode->i_op->get_link) {
        const char *lnk = inode->i_op->get_link(dentry, inode);
        if (IS_ERR(lnk)) {
            err = PTR_ERR(lnk);
            goto error;
        }
        int len = strlen(lnk);
        if (len > bufsize) {
            err = -ERANGE;
            goto error;
        }
        err = copy_to_user(buf, lnk, len);
        if (err < 0)
            goto error;
        err = len;
    } else {
        err = -EINVAL;
    }

error:
    if (!IS_ERR_OR_NULL(dentry))
        dput(dentry);
    kfree(path_buf);
    return err;
}

struct iovec {
    void *iov_base;	/* Pointer to data.  */
    size_t iov_len;	/* Length of data.  */
};

SYSCALL_DECL3(readv, int, fd, const struct iovec __user *, iov, int, iovcnt)
{
    struct file *file;
    struct iovec kiov;
    ssize_t total = 0;

    if (iovcnt < 0 || iovcnt > 1024)
        return -EINVAL;

    file = get_file_handle(fd);
    if (IS_ERR(file))
        return PTR_ERR(file);

    if ((file->f_mode & O_ACCMODE) == O_WRONLY)
        return -EBADF;

    for (int i = 0; i < iovcnt; i++) {
        if (copy_from_user(&kiov, &iov[i], sizeof(kiov)))
            return -EFAULT;
        if (!kiov.iov_len)
            continue;

        unsigned char *kbuf = kmalloc(kiov.iov_len);
        if (!kbuf)
            return total > 0 ? total : -ENOMEM;

        ssize_t bytes = vfs_read(file, kbuf, kiov.iov_len);
        if (bytes > 0) {
            if (copy_to_user(kiov.iov_base, kbuf, bytes)) {
                kfree(kbuf);
                return total > 0 ? total : -EFAULT;
            }
            total += bytes;
        }
        kfree(kbuf);
        if (bytes < 0)
            return total > 0 ? total : bytes;
        if ((size_t)bytes < kiov.iov_len)
            break;
    }
    return total;
}

SYSCALL_DECL3(writev, int, fd, const struct iovec __user *, iov, int, iovcnt)
{
    struct file *file;
    struct iovec kiov;
    ssize_t total = 0;

    if (iovcnt < 0 || iovcnt > 1024)
        return -EINVAL;

    file = get_file_handle(fd);
    if (IS_ERR(file))
        return PTR_ERR(file);

    if ((file->f_mode & O_ACCMODE) == O_RDONLY)
        return -EBADF;

    for (int i = 0; i < iovcnt; i++) {
        if (copy_from_user(&kiov, &iov[i], sizeof(kiov)))
            return -EFAULT;
        if (!kiov.iov_len)
            continue;

        unsigned char *kbuf = kmalloc(kiov.iov_len);
        if (!kbuf)
            return total > 0 ? total : -ENOMEM;

        if (copy_from_user(kbuf, kiov.iov_base, kiov.iov_len)) {
            kfree(kbuf);
            return total > 0 ? total : -EFAULT;
        }

        ssize_t bytes = vfs_write(file, kbuf, kiov.iov_len);
        kfree(kbuf);
        if (bytes > 0)
            total += bytes;
        if (bytes < 0)
            return total > 0 ? total : bytes;
        if ((size_t)bytes < kiov.iov_len)
            break;
    }
    return total;
}

// Positional I/O only makes sense on seekable files
static bool file_is_seekable(struct file *file)
{
    if (!file->f_dentry)
        return false;
    umode_t mode = file->f_dentry->d_inode->i_mode;
    return !S_ISCHR(mode) && !S_ISFIFO(mode);
}

SYSCALL_DECL4(pread64, int, fd, void*, buf, size_t, count, off_t, offset)
{
    struct file *file = get_file_handle(fd);
    if (IS_ERR(file))
        return PTR_ERR(file);
    if ((file->f_mode & O_ACCMODE) == O_WRONLY)
        return -EBADF;
    if (!file_is_seekable(file))
        return -ESPIPE;
    if (offset < 0)
        return -EINVAL;
    if (count == 0)
        return 0;

    unsigned char *kbuf = kmalloc(count);
    if (!kbuf)
        return -ENOMEM;

    ssize_t bytes = vfs_read_at(file, kbuf, count, offset);
    if (bytes > 0 && copy_to_user(buf, kbuf, bytes))
        bytes = -EFAULT;

    kfree(kbuf);
    return bytes;
}

SYSCALL_DECL4(pwrite64, int, fd, const void*, buf, size_t, count, off_t, offset)
{
    struct file *file = get_file_handle(fd);
    if (IS_ERR(file))
        return PTR_ERR(file);
    if ((file->f_mode & O_ACCMODE) == O_RDONLY)
        return -EBADF;
    if (!file_is_seekable(file))
        return -ESPIPE;
    if (offset < 0)
        return -EINVAL;
    if (count == 0)
        return 0;

    unsigned char *kbuf = kmalloc(count);
    if (!kbuf)
        return -ENOMEM;

    if (copy_from_user(kbuf, buf, count)) {
        kfree(kbuf);
        return -EFAULT;
    }

    ssize_t bytes = vfs_write_at(file, kbuf, count, offset);
    kfree(kbuf);
    return bytes;
}
