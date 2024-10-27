// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/fs.h>

#include <lilac/syscall.h>
#include <lilac/log.h>
#include <lilac/panic.h>
#include <lilac/device.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/device.h>
#include <lilac/timer.h>
#include <drivers/blkdev.h>
#include <fs/fat32.h>
#include <fs/tmpfs.h>
#include <mm/kmalloc.h>

#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include "utils.h"

static struct dentry *
(*init_ops[4])(void*, struct super_block*) = {
    fat32_init, NULL, tmpfs_init, NULL
};

struct dentry *root_dentry = NULL;
static u32 numdisks = 0;
static struct vfsmount disks[8];

static int get_fd(void)
{
    if (current->fs.files.size < 256)
        return current->fs.files.size++;
    return -1;
}

static enum fs_type str_to_fs(const char *fs_type)
{
    if (!strcmp(fs_type, "msdos"))
        return MSDOS;
    else if (!strcmp(fs_type, "tmpfs"))
        return TMPFS;
    else
        return -1;
}

static void root_init(struct block_device *bdev)
{
    struct super_block *sb = kzmalloc(sizeof(struct super_block));
    struct vfsmount *root_disk = &disks[numdisks++];

    root_disk->mnt_sb = sb;
    root_disk->init_fs = init_ops[bdev->type];

    root_dentry = root_disk->init_fs(bdev, sb);
    if (!root_dentry)
        kerror("Failed to initialize root dentry\n");
    root_disk->mnt_root = root_dentry;

    root_dentry->d_parent = NULL;
    root_dentry->d_name = "/";
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

    mount("tmpfs", "/tmp", "tmpfs", 0, NULL);
    mount("tmpfs", "/dev", "tmpfs", 0, NULL);
    create("/dev/null", 0);
    create("/dev/zero", 0);

    kstatus(STATUS_OK, "Filesystem initialized\n");
}

inline struct inode *get_root_inode(void)
{
    return root_dentry->d_inode;
}

// struct dentry *mount_bdev(struct block_device *bdev)
// {
//     struct vfsmount *mnt = &disks[numdisks++];
//     struct super_block *sb = kzmalloc(sizeof(struct super_block));
//     mnt->init_fs = init_ops[bdev->type];
//     mnt->mnt_sb = sb;

//     mnt->init_fs(bdev, sb);

//     return 0;
// }

int mount(const char *source, const char *target,
        const char *filesystemtype, unsigned long mountflags,
        const void *data)
{
    void *device = NULL; // str_to_device(source);
    struct super_block *sb;
    struct dentry *dentry;
    struct vfsmount *mnt;
    char dirname[64];
    char basename[16];
    enum fs_type type = str_to_fs(filesystemtype);
    if (type == -1 || numdisks >= 8)
        return -1;

    memset(dirname, 0, 64);
    memset(basename, 0, 16);
    get_dirname(dirname, target);
    get_basename(basename, target);

    // Check that the target's parent directory exists
    struct dentry *parent = lookup_path(dirname);
    if (!parent)
        return -1;

    // Check that the target is a valid directory, or create it
    struct dentry *new_dentry = lookup_path_from(parent, basename);
    struct inode *new_inode = new_dentry->d_inode;
    if (new_inode && new_inode->i_type != TYPE_DIR) {
        klog(LOG_ERROR, "Target is not a directory\n");
        return -1;
    }
    if (!new_dentry->d_inode) {
        klog(LOG_DEBUG, "Creating new dir %s\n", new_dentry->d_name);
        parent->d_inode->i_op->mkdir(parent->d_inode, new_dentry, 0);
    }

    sb = kzmalloc(sizeof(struct super_block));
    dentry = init_ops[type](device, sb);
    if (!dentry) {
        kfree(sb);
        return -1;
    }
    sb->s_type = type;
    sb->s_root = dentry;
    sb->s_active = true;
    sb->s_count = 1;

    dentry->d_name = kzmalloc(strlen(basename)+1);
    strcpy(dentry->d_name, basename);
    dentry->d_parent = parent;
    dentry->d_sb = sb;

    mnt = &disks[numdisks++];
    mnt->mnt_sb = sb;
    mnt->mnt_root = dentry;
    mnt->init_fs = init_ops[type];
    new_dentry->d_mount = mnt;

    klog(LOG_DEBUG, "Mounted %s on %s\n", source, target);

    return 0;
}
SYSCALL_DECL5(mount, const char*, source, const char*, target,
    const char*, filesystemtype, unsigned long, mountflags,
    const void*, data)

#define SEEK_SET 0
#define SEEK_CUR 1
// #define SEEK_END 2

int lseek(int fd, int offset, int whence)
{
    struct file *file = current->fs.files.fdarray[fd];
    if (whence == SEEK_SET)
        file->f_pos = offset;
    else if (whence == SEEK_CUR)
        file->f_pos += offset;
    // else if (whence == SEEK_END)
    //     file->f_pos = file->f_inode->i_size + offset;
    else
        return -1;

    return file->f_pos;
}

int open(const char *path, int flags, int mode)
{
    klog(LOG_DEBUG, "VFS: Opening %s\n", path);
    int n_pos = 0;
    int n_len = strlen(path);
    int fd;
    struct inode *inode;
    struct file *new_file;

    new_file = kzmalloc(sizeof(*new_file));
    new_file->f_path = kzmalloc(n_len+1);
    strcpy(new_file->f_path, path);

    // Is this all open is supposed to do?
    struct dentry *dentry = lookup_path(path);
    if (!dentry)
        return -1;
    inode = dentry->d_inode;
    if (!inode)
        return -1;
    if(inode->i_op->open(inode, new_file))
        return -1;

    fd = get_fd();
    current->fs.files.fdarray[fd] = new_file;

    return fd;
}
SYSCALL_DECL3(open, const char*, path, int, flags, int, mode)


ssize_t read(int fd, void *buf, size_t count)
{
    struct file *file = current->fs.files.fdarray[fd];

    if (file->f_inode->i_type == TYPE_DEV) {
        return file->f_inode->i_fop->read(file, buf, count);
    }

    // while (file->f_pos >= file->f_inode->i_size) {
    //     klog(LOG_DEBUG, "VFS: Blocking read on file %s\n", file->f_path);
    //     // current->state = TASK_SLEEPING;
    //     // schedule();
    //     sleep(1000);
    // }

    ssize_t bytes = file->f_op->read(file, buf, count);
    if (bytes > 0)
        file->f_pos += bytes;
    return bytes;
}
SYSCALL_DECL3(read, int, fd, void*, buf, size_t, count)


ssize_t write(int fd, const void *buf, size_t count)
{
    struct file *file = current->fs.files.fdarray[fd];

    if (file->f_inode->i_type == TYPE_DEV) {
        return file->f_inode->i_fop->write(file, buf, count);
    }

    ssize_t bytes = file->f_op->write(file, buf, count);
    if (bytes > 0)
        file->f_pos += bytes;
    return bytes;
}
SYSCALL_DECL3(write, int, fd, const void*, buf, size_t, count)


int close(int fd)
{
    struct file *file = current->fs.files.fdarray[fd];
    //file->f_op->flush(file);
    current->fs.files.fdarray[fd] = NULL;
    current->fs.files.size--;
    if (--file->f_count)
        return 0;

    //file->f_op->release(file->f_inode, file);
    kfree(file->f_path);
    kfree(file);
    return 0;
}
SYSCALL_DECL1(close, int, fd)


ssize_t getdents(unsigned int fd, struct dirent *dirp, unsigned int buf_size)
{
    struct file *file = current->fs.files.fdarray[fd];
    struct inode *inode = file->f_inode;

    if (inode->i_type != TYPE_DIR)
        return -1;

    klog(LOG_DEBUG, "VFS: Reading directory %s\n", file->f_path);
    memset(dirp, 0, buf_size);
    int cnt = file->f_op->readdir(file, dirp, buf_size);
    file->f_pos += cnt;
    return cnt > 0 ? cnt * sizeof(struct dirent) : cnt;
}
SYSCALL_DECL3(getdents, unsigned int, fd, struct dirent*, dirp, size_t, buf_size)

int mkdir(const char *path, umode_t mode)
{
    struct inode *parent;
    struct dentry *new_dentry;

    char name[16];
    char dir_path[64];
    char *p = strrchr(path, '/');
    if (!p)
        return -1;
    if (!*(p+1))
        p = strrchr(p-1, '/');

    strcpy(name, p+1);
    strncpy(dir_path, path, (uintptr_t)p - (uintptr_t)path);

    struct dentry *parent_dentry = lookup_path(dir_path);
    if (!parent_dentry)
        return -1;
    parent = parent_dentry->d_inode;
    if (!parent)
        return -1;

    new_dentry = kzmalloc(sizeof(*new_dentry));
    new_dentry->d_parent = parent_dentry;
    new_dentry->d_sb = parent->i_sb;
    strcpy(new_dentry->d_name, name);
    dcache_add(new_dentry);

    return parent->i_op->mkdir(parent, new_dentry, mode);
}
SYSCALL_DECL2(mkdir, const char*, path, umode_t, mode)


int create(const char *path, umode_t mode)
{
    char dirname[64];
    char basename[16];
    struct inode *parent_inode;
    struct super_block *sb;

    memset(dirname, 0, 64);
    memset(basename, 0, 16);
    get_dirname(dirname, path);
    get_basename(basename, path);
    struct dentry *parent = lookup_path(dirname);
    if (!parent)
        return -1;
    parent_inode = parent->d_inode;
    if (!parent_inode)
        return -1;
    sb = parent_inode->i_sb;

    struct dentry *new_dentry = kzmalloc(sizeof(*new_dentry));
    new_dentry->d_name = kzmalloc(strlen(basename)+1);
    strcpy(new_dentry->d_name, basename);
    new_dentry->d_parent = parent;
    new_dentry->d_sb = sb;
    dcache_add(new_dentry);

    klog(LOG_DEBUG, "Creating %s\n", new_dentry->d_name);

    return parent_inode->i_op->create(parent_inode, new_dentry, mode);
}
SYSCALL_DECL2(create, const char*, path, umode_t, mode)

int mknod(const char *pathname, int mode, dev_t dev)
{
    return -1;
}
SYSCALL_DECL3(mknod, const char*, pathname, int, mode, dev_t, dev)
