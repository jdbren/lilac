// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <lilac/log.h>
#include <lilac/panic.h>
#include <lilac/fs.h>
#include <lilac/device.h>
#include <drivers/blkdev.h>
#include <fs/fat32.h>
#include <mm/kheap.h>

static struct dentry *
(*init_ops[4])(struct block_device*, struct super_block*) = {
    fat32_init
};

static struct vfsmount root_disk;
static struct dentry *root_dentry;

static int numdisks = 0;
static struct vfsmount disks[8];
static struct file *vnodes[256];

static int get_fd(void)
{
    for (int i = 0; i < 256; i++) {
        if (vnodes[i] == NULL)
            return i;
    }
    return -1;
}

static int name_len(const char *path, int n_pos)
{
    int i = 0;
    while (path[n_pos] != '/' && path[n_pos] != '\0') {
        i++; n_pos++;
    }
    return i;
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
    // create_dev_files();
    kstatus(STATUS_OK, "Filesystem initialized\n");
    return 0;
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
    struct file *file = vnodes[fd];
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
    int n_pos = 0;
    int n_len = strlen(path);
    int fd;
    struct inode *parent;
    struct dentry *current = root_dentry;
    struct dentry *find;
    struct file *new_file;

    if (path[n_pos] != '/')
        return -1;

    while (path[n_pos++] != '\0') {
        int len = name_len(path, n_pos);
        char *name = kzmalloc(len+1);
        find = kzmalloc(sizeof(*find));

        parent = current->d_inode;
        find->d_parent = current;
        strncpy(name, path + n_pos, len);
        name[len] = '\0';
        find->d_name = name;

        parent->i_op->lookup(parent, find, 0);
        // dcache_add(find);

        if (find->d_inode == NULL)
            return -1;

        current = find;
        n_pos += len;
    }

    new_file = kzmalloc(sizeof(*new_file));
    new_file->f_path = kzmalloc(n_len+1);
    strcpy(new_file->f_path, path);

    // Is this all open is supposed to do?
    parent = current->d_inode;
    if(parent->i_op->open(parent, new_file))
        return -1;

    // should add to current process's file table
    fd = get_fd();
    vnodes[fd] = new_file;

    return fd;
}

ssize_t read(int fd, void *buf, size_t count)
{
    struct file *file = vnodes[fd];
    return file->f_op->read(file, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    struct file *file = vnodes[fd];
    return file->f_op->write(file, buf, count);
}

// int close(int fd)
// {
//     struct file *file = vnodes[fd];
//     file->f_op->flush(file);
//     if (--file->f_count)
//         return 0;

//     file->f_op->release(file->f_inode, file);
//     kfree(file->f_path);
//     kfree(file);
//     vnodes[fd] = NULL;
//     return 0;
// }
