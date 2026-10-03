#include <lilac/fs.h>
#include <lilac/log.h>
#include <lilac/err.h>
#include <lilac/timer.h>
#include <lilac/libc.h>
#include <lilac/sync.h>
#include <fs/tmpfs.h>
#include "tmpfs_internal.h"

#include <mm/kmalloc.h>

static int unique_ino(void)
{
    static unsigned long ino = 1;
    return ++ino;
}

static struct inode* tmpfs_alloc_inode(struct super_block *sb)
{
    struct inode *inode = kzmalloc(sizeof(struct inode));
    if (!inode) {
        klog(LOG_ERROR, "tmpfs_alloc_inode: Out of memory allocating inode\n");
        return ERR_PTR(-ENOMEM);
    }

    inode->i_ino = unique_ino();
    inode->i_sb = sb;
    mutex_init(&inode->i_mutex);
    inode->i_op = &tmpfs_iops;
    inode->i_count = 1;
    inode->i_atime = inode->i_mtime = inode->i_ctime = get_unix_time();
    inode->i_nlink = 1;
    list_add_tail(&inode->i_list, &sb->s_inodes);

    return inode;
}

static void tmpfs_destroy_inode(struct inode *inode)
{
    if (inode->i_private) {
        if (S_ISDIR(inode->i_mode)) {
            struct tmpfs_dir *dir = inode->i_private;
            kfree(dir->children);
        } else {
            struct tmpfs_file *file = inode->i_private;
            kfree(file->data);
        }
        kfree(inode->i_private);
    }
    kfree(inode);
}

struct dentry* tmpfs_init(void *device, struct super_block *sb)
{
    klog(LOG_DEBUG, "Initializing tmpfs\n");
    sb->s_type = TMPFS;
    sb->s_op = &tmpfs_sops;
    sb->s_blocksize = 0x1000;
    sb->s_maxbytes = __INT32_MAX__;

    struct dentry *root_dentry = kzmalloc(sizeof(struct dentry));
    if (!root_dentry) {
        klog(LOG_ERROR, "tmpfs_init: Failed to allocate root dentry\n");
        return ERR_PTR(-ENOMEM);
    }
    struct inode *root_inode = tmpfs_alloc_inode(sb);
    if (IS_ERR_OR_NULL(root_inode)) {
        klog(LOG_ERROR, "tmpfs_init: Failed to allocate root inode\n");
        kfree(root_dentry);
        return ERR_CAST(root_inode);
    }
    struct tmpfs_dir *root_dir = kzmalloc(sizeof(struct tmpfs_dir));
    if (!root_dir) {
        klog(LOG_ERROR, "tmpfs_init: Failed to allocate root tmpfs_dir\n");
        tmpfs_destroy_inode(root_inode);
        kfree(root_dentry);
        return ERR_PTR(-ENOMEM);
    }

    root_inode->i_private = root_dir;
    root_inode->i_mode = S_IFDIR|S_IREAD|S_IWRITE|S_IEXEC;
    root_dentry->d_count = 1;
    mutex_init(&root_dentry->d_lock);
    root_dentry->d_sb = sb;
    root_dentry->d_inode = root_inode;
    sb->s_root = root_dentry;

    return root_dentry;
}

const struct super_operations tmpfs_sops = {
    .alloc_inode = tmpfs_alloc_inode,
    .destroy_inode = tmpfs_destroy_inode
};

// Internal tmpfs instance backing files that have no path
static struct super_block *anon_sb;
static spinlock_t anon_sb_lock = SPINLOCK_INIT;

static struct super_block * get_anon_sb(void)
{
    struct super_block *sb;

    acquire_lock(&anon_sb_lock);
    if (!anon_sb) {
        sb = alloc_sb(NULL);
        if (IS_ERR(sb))
            goto out;
        struct dentry *root = tmpfs_init(NULL, sb);
        if (IS_ERR(root)) {
            destroy_sb(sb);
            sb = ERR_CAST(root);
            goto out;
        }
        anon_sb = sb;
    }
    sb = anon_sb;
out:
    release_lock(&anon_sb_lock);
    return sb;
}

// Create an unlinked, read-write tmpfs file
struct file * tmpfs_anon_file(const char *name)
{
    struct super_block *sb = get_anon_sb();
    if (IS_ERR(sb))
        return ERR_CAST(sb);

    struct tmpfs_file *file_info = kzmalloc(sizeof(*file_info));
    if (!file_info)
        return ERR_PTR(-ENOMEM);
    file_info->data = kmalloc(4096);
    if (!file_info->data) {
        kfree(file_info);
        return ERR_PTR(-ENOMEM);
    }

    struct inode *inode = tmpfs_alloc_inode(sb);
    if (IS_ERR(inode)) {
        kfree(file_info->data);
        kfree(file_info);
        return ERR_CAST(inode);
    }
    inode->i_private = file_info;
    inode->i_mode = S_IFREG | S_IREAD | S_IWRITE;
    inode->i_size = 0;
    inode->i_nlink = 0;

    char dname[NAME_MAX];
    snprintf(dname, sizeof(dname), "memfd:%s", name);
    // Not added to the root directory's entries, so lookups never find it
    struct dentry *dentry = alloc_dentry(sb->s_root, dname);
    if (IS_ERR(dentry)) {
        iput(inode);
        return ERR_CAST(dentry);
    }
    dentry->d_inode = inode;

    struct file *file = alloc_file(dentry);
    dput(dentry);
    if (!file)
        return ERR_PTR(-ENOMEM);
    file->f_op = &tmpfs_fops;
    file->f_mode = O_RDWR;

    return file;
}
