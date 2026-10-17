#include <lilac/fs.h>
#include <lilac/panic.h>
#include <lilac/timer.h>

static const struct super_operations anon_sops;
static struct super_block *anon_sb;

void anon_fs_init(void)
{
    anon_sb = alloc_sb(NULL);
    if (IS_ERR(anon_sb))
        kerror("Failed to allocate the anonymous inode superblock\n");
    anon_sb->s_op = &anon_sops;
}

struct inode * new_anon_inode(umode_t mode, const struct file_operations *fops)
{
    static atomic_ulong next_ino = 1;

    struct inode *inode = alloc_inode(anon_sb);
    if (!inode)
        return NULL;
    inode->i_ino = next_ino++;
    inode->i_mode = mode;
    inode->i_fop = fops;
    inode->i_atime = inode->i_mtime = inode->i_ctime = get_unix_time();
    return inode;
}

struct file * alloc_anon_file(struct inode *inode, int flags, void *priv)
{
    struct file *file = alloc_file(NULL, inode);
    if (!file)
        return NULL;
    file->f_op = inode->i_fop;
    file->f_mode = flags & (O_ACCMODE | O_NONBLOCK);
    file->f_data = priv;
    return file;
}
