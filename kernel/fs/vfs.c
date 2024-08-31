// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <lilac/fs.h>
#include <lilac/file.h>

#include <lilac/syscall.h>
#include <lilac/log.h>
#include <lilac/panic.h>
#include <lilac/device.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/device.h>
#include <drivers/blkdev.h>
#include <fs/fat32.h>
#include <mm/kmalloc.h>

#include <stdbool.h>
#include <string.h>
#include <ctype.h>

static struct dentry *
(*init_ops[4])(struct block_device*, struct super_block*) = {
    fat32_init
};

static struct vfsmount root_disk;
struct dentry *root_dentry;

static int numdisks = 0;
static struct vfsmount disks[8];
// static struct file *vnodes[256];

static int get_fd(void)
{
    if (current->fs.files.size < 256)
        return current->fs.files.size++;
    return -1;
}

static enum fs_type str_to_fs(const char *fs_type)
{
    if (strcmp(fs_type, "msdos") == 0)
        return MSDOS;
    else
        return -1;
}

static void root_init(struct block_device *bdev)
{
    struct super_block *sb = kzmalloc(sizeof(struct super_block));

    root_disk.mnt_sb = sb;
    root_disk.init_fs = init_ops[bdev->type];

    root_dentry = root_disk.init_fs(bdev, sb);
    if (!root_dentry)
        kerror("Failed to initialize root dentry\n");
    root_disk.mnt_root = root_dentry;

    root_dentry->d_parent = root_dentry;
    root_dentry->d_name = "/";
    dcache_add(root_dentry);

    klog(LOG_INFO, "%s mounted on /\n", bdev->name);
}

int fs_init(void)
{
    dev_t devt = DEVICE_ID(SATA_DEVICE, 0);
    struct block_device *bdev;

    scan_partitions(NULL);
    bdev = get_bdev(devt);
    if (!bdev)
        kerror("Failed to get root block device\n");

    root_init(bdev);
    disks[numdisks++] = root_disk;

    dev_files_init();

    kstatus(STATUS_OK, "Filesystem initialized\n");
    return 0;
}

struct inode *get_root_inode(void)
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

// Need to implement dcache, currently leaks memory
int open(const char *path, int flags, int mode)
{
    klog(LOG_DEBUG, "VFS: Opening %s\n", path);
    int n_pos = 0;
    int n_len = strlen(path);
    int fd;
    struct inode *parent;
    struct file *new_file;

    new_file = kzmalloc(sizeof(*new_file));
    new_file->f_path = kzmalloc(n_len+1);
    strcpy(new_file->f_path, path);

    // Is this all open is supposed to do?
    parent = lookup_path(path);
    if (!parent)
        return -1;
    if(parent->i_op->open(parent, new_file))
        return -1;

    fd = get_fd();
    current->fs.files.fdarray[fd] = new_file;

    return fd;
}
SYSCALL_DECL3(open, const char*, path, int, flags, int, mode)


ssize_t read(int fd, void *buf, size_t count)
{
    struct file *file = current->fs.files.fdarray[fd];
    return file->f_op->read(file, buf, count);
}
SYSCALL_DECL3(read, int, fd, void*, buf, size_t, count)


ssize_t write(int fd, const void *buf, size_t count)
{
    printf("%c", *(char*)buf);
    return 1;
    struct file *file = current->fs.files.fdarray[fd];
    return file->f_op->write(file, buf, count);
}
SYSCALL_DECL3(write, int, fd, const void*, buf, size_t, count)


int close(int fd)
{
    struct file *file = current->fs.files.fdarray[fd];
    file->f_op->flush(file);
    if (--file->f_count)
        return 0;

    file->f_op->release(file->f_inode, file);
    kfree(file->f_path);
    kfree(file);
    current->fs.files.fdarray[fd] = NULL;
    return 0;
}
SYSCALL_DECL1(close, int, fd)


int getdents(unsigned int fd, struct dirent *dirp, unsigned int buf_size)
{
    struct file *file = current->fs.files.fdarray[fd];
    struct inode *inode = file->f_inode;

    if (inode->i_type != TYPE_DIR)
        return -1;

    klog(LOG_DEBUG, "VFS: Reading directory %s\n", file->f_path);
    memset(dirp, 0, buf_size);
    return file->f_op->readdir(file, dirp, buf_size);
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

    parent = lookup_path(dir_path);
    if (!parent)
        return -1;
    struct file *new_file = kzmalloc(sizeof(*new_file));
    new_file->f_path = kzmalloc(strlen(path)+1);
    strcpy(new_file->f_path, path);
    parent->i_op->open(parent, new_file);

    new_dentry = kzmalloc(sizeof(*new_dentry));
    strcpy(new_dentry->d_name, name);
    dcache_add(new_dentry);

    return parent->i_op->mkdir(parent, new_dentry, mode);
}
