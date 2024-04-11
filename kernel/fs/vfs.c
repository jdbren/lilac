// Copyright (C) 2024 Jackson Brenneman
// GPL-3.0-or-later (see LICENSE.txt)
#include <stdbool.h>
#include <string.h>
#include <lilac/fs.h>
#include <drivers/blkdev.h>
#include <fs/fat32.h>
#include <mm/kheap.h>

static struct fs_operations ops[4] = {
    {
        .init_fs = &fat32_init,
        .open = &fat32_open,
    },
    {
        .open = NULL,
    },
    {
        .open = NULL,
    },
    {
        .open = NULL,
    }
};

static int numdisks = 0;
static struct vfsmount disks[8];
static struct vnode *vnodes[256];

static int get_fd(void)
{
    for (int i = 0; i < 256; i++) {
        if (vnodes[i] == NULL)
            return i;
    }
    return -1;
}

static struct vnode *mk_vnode(int dev, struct file *f)
{
    struct vnode *vnode = kzmalloc(sizeof(struct vnode));
    vnode->disknum = dev;
    vnode->type = VNODE_FILE;
    vnode->f = f;
    return vnode;
}

// enum fs_type str_to_fs(const char *fs_type)
// {
//     if (strcmp(fs_type, "msdos") == 0)
//         return MSDOS;
//     else
//         return -1;
// }

int fs_mount(struct block_device *bdev, enum fs_type type)
{
    struct vfsmount *mnt = &disks[numdisks];
    mnt->num = numdisks++;
    //strcpy(disk->devname, dev_name);
    mnt->ops = &ops[type];
    mnt->bdev = bdev;

    mnt->ops->init_fs(mnt);

    return 0;
}

int open(const char *path, int flags, int mode)
{
    int fd;
    struct vfsmount *disk = &disks[path[0] - 'A'];
    struct file *new_file = kzmalloc(sizeof(*new_file));
    new_file->f_path = path;
    new_file->f_disk = disk;

    disk->ops->open(path+2, new_file);
    if (new_file == NULL) {
        kfree(new_file);
        return -1;
    }
    fd = get_fd();
    vnodes[fd] = mk_vnode(disk->num, new_file);

    return fd;
}

ssize_t read(int fd, void *buf, size_t count)
{
    struct vnode *vnode = vnodes[fd];
    if (vnode->type != VNODE_FILE)
        return -1;
    printf("Reading from disk %d\n", vnode->disknum);
    return vnode->f->f_op->read(vnode->f, buf, count);
}
