// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <stdbool.h>
#include <fs/vfs.h>
#include <fs/fat32.h>
#include <mm/kheap.h>

struct file_ops {
    size_t (*f_read)(int, void*, size_t);
    size_t (*f_write)(int, const void*, size_t);
};

struct vnode {
    struct file_ops *f_op;
    int dev;
    int f_flags;
    int f_mode;
    int inode;
};

struct disk {
    int dev;
    enum fs_type type;
    char id;
    bool boot_dev;
};

static struct file_ops ops[4] = {
    {&fat32_read, NULL},
    {NULL,  NULL},
    {NULL,  NULL},
    {NULL,  NULL},
};

static int disknum = 0;
static struct disk disks[16];
static struct vnode *vnodes[256];

static int get_fd()
{
    for (int i = 0; i < 256; i++) {
        if (vnodes[i] == NULL)
            return i;
    }
    return -1;
}

static struct vnode* mk_vnode(int dev, int flags, int mode, int data)
{
    struct vnode *vnode = kmalloc(sizeof(struct vnode));
    vnode->dev = dev;
    vnode->f_flags = flags;
    vnode->f_mode = mode;
    vnode->inode = data;
    vnode->f_op = &ops[disks[dev].type];
    return vnode;
}

void vfs_install_disk(int type, bool boot)
{
    struct disk *disk = &disks[disknum];
    disk->dev = disknum;
    disk->type = type;
    disk->id = 'A' + disknum;
    disk->boot_dev = boot;
    disknum++;
}

int open(const char *path, int flags, int mode)
{
    int fd = 0;
    char id = path[0];
    struct disk *disk = &disks[0];
    while (disk->id != id)
        disk++;
    if (disk->type == FAT) {
        int inode = fat32_open(path+2);
        if (inode < 0)
            return -1;
        fd = get_fd();
        vnodes[fd] = mk_vnode(disk->dev, flags, mode, inode);
        return fd;
    }
    else {
        printf("Filesystem not supported\n");
        return -1;
    }
}

size_t read(int fd, void *buf, size_t count)
{
    struct vnode *vnode = vnodes[fd];
    return vnode->f_op->f_read(vnode->inode, buf, count);
}
