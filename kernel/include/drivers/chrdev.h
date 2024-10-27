#ifndef _LILAC_CHRDEV_H
#define _LILAC_CHRDEV_H

#include <lilac/types.h>
#include <lilac/sync.h>

struct char_device {
    dev_t devnum;
    u32 size;
    char name[32];
    struct inode *cd_inode;
    struct char_device *next;
    // struct mutex cd_holder_lock;
};

#endif
