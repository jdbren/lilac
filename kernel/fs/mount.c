#include <lilac/fs.h>
#include <lilac/lilac.h>
#include <lilac/syscall.h>
#include <drivers/blkdev.h>

#include "utils.h"

static u32 numdisks = 0;
static struct vfsmount disks[16];

struct vfsmount * get_empty_vfsmount(enum fs_type type)
{
    for (int i = 0; i < 16; i++) {
        if (disks[i].mnt_sb == NULL) {
            disks[i].init_fs = get_fs_init(type);
            numdisks++;
            return &disks[i];
        }
    }
    return NULL;
}

static int validate_params(const char *source, const char *target,
    enum fs_type type, unsigned long mountflags)
{
    if (type < -1)
        return -ENODEV;
    if (numdisks >= 8)
        return -ENOMEM;
    return 0;
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

static struct dentry * get_parent_dentry(const char *path)
{
    char *dirname = kzmalloc(64);
    if (!dirname)
        return ERR_PTR(-ENOMEM);
    if (get_dirname(dirname, path, 64)) {
        kfree(dirname);
        return ERR_PTR(-EINVAL);
    }
    struct dentry *parent = lookup_path(dirname);
    kfree(dirname);
    return parent;
}

static struct dentry * get_final_dentry(struct dentry * parent, const char *path)
{
    char basename[16];
    if (get_basename(basename, path, 16)) {
        return ERR_PTR(-EINVAL);
    }
    return lookup_path_from(parent, basename);
}

// TODO: Implement device layer in fs
int vfs_mount(const char *source, const char *target,
        const char *filesystemtype, unsigned long mountflags,
        const void *data)
{
    void *device = NULL; // str_to_device(source);
    struct super_block *sb;
    struct dentry *dentry;
    struct vfsmount *mnt;
    enum fs_type type = str_to_fs(filesystemtype);
    long err = 0;

    err = validate_params(source, target, type, mountflags);
    if (err)
        return err;

    mnt = get_empty_vfsmount(type);
    if (!mnt)
        return -ENOMEM;

    // Check that the target's parent directory exists
    struct dentry *parent = get_parent_dentry(target);
    if (IS_ERR(parent))
        return PTR_ERR(parent);

    // Check that the target is a valid directory, or create it
    struct dentry *new_dentry = get_final_dentry(parent, target);
    if (IS_ERR(new_dentry))
        return PTR_ERR(new_dentry);

    struct inode *new_inode = new_dentry->d_inode;
    if (new_inode && new_inode->i_type != TYPE_DIR) {
        klog(LOG_DEBUG, "Mount: target is not a directory\n");
        return -ENOTDIR;
    }
    if (!new_dentry->d_inode) {
        klog(LOG_DEBUG, "Creating new dir %s\n", new_dentry->d_name);
        parent->d_inode->i_op->mkdir(parent->d_inode, new_dentry, 0);
    }

    struct block_device *bdev = kzmalloc(sizeof(struct block_device));
    bdev->type = type;
    sb = alloc_sb(bdev);
    if (IS_ERR(sb))
        return PTR_ERR(sb);

    dentry = mnt->init_fs(device, sb); // Todo: add error handling
    if (IS_ERR_OR_NULL(dentry)) {
        destroy_sb(sb);
        return -ENODEV;
    }
    sb->s_root = dentry;

    dentry->d_name = kzmalloc(strlen(new_dentry->d_name)+1);
    strcpy(dentry->d_name, new_dentry->d_name);
    dentry->d_parent = parent;
    dentry->d_sb = sb;

    mnt->mnt_sb = sb;
    mnt->mnt_root = dentry;
    new_dentry->d_mount = mnt;

    klog(LOG_DEBUG, "Mounted %s on %s\n", source, target);

    return 0;
}

SYSCALL_DECL5(mount, const char*, source, const char*, target,
    const char*, filesystemtype, unsigned long, mountflags,
    const void*, data)
{
    return -ENOSYS;
/*
    long err = 0;

    err = user_str_ok(source, 255);
    if (err < 0)
        return err;

    err = user_str_ok(target, 255);
    if (err < 0)
        return err;

    err = user_str_ok(filesystemtype, 255);
    if (err < 0)
        return err;

    if (!access_ok(data, 1)) {
        klog(LOG_WARN, "mount: data pointer not accessible\n");
        return -EFAULT;
    }

    err = vfs_mount(source, target, filesystemtype, mountflags, data);
    return err;
*/
}

int vfs_umount(const char *target)
{
    struct dentry *dentry = lookup_path(target);
    if (IS_ERR(dentry)) {
        klog(LOG_DEBUG, "Failed to find target %s for umount\n", target);
        return PTR_ERR(dentry);
    }

    struct vfsmount *mnt = dentry->d_mount;
    if (!mnt) {
        klog(LOG_DEBUG, "No mount found for %s\n", target);
        return -EINVAL;
    }

    // Perform unmount operations
    dput(mnt->mnt_root);
    destroy_sb(mnt->mnt_sb);
    mnt->mnt_root = NULL;
    mnt->mnt_sb = NULL;

    klog(LOG_DEBUG, "Unmounted %s\n", target);
    return 0;
}

SYSCALL_DECL1(umount, const char *, target)
{
    return -ENOSYS;
}
