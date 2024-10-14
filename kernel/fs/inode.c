#include <lilac/fs.h>

#include <string.h>

#include <lilac/log.h>
#include <mm/kmalloc.h>

#define L1_CACHE_BYTES 64

#define GOLDEN_RATIO_32 0x61C88647UL
#define GOLDEN_RATIO_64 0x61C8864680B583EBULL

#if BITS_PER_LONG == 32
#define GOLDEN_RATIO_NUM GOLDEN_RATIO_32
#elif BITS_PER_LONG == 64
#define GOLDEN_RATIO_NUM GOLDEN_RATIO_64
#endif

static const unsigned long i_hash_shift = 16;
static const unsigned long i_hash_mask = (1 << i_hash_shift) - 1;

static unsigned long hash(struct super_block *sb, unsigned long hashval)
{
    unsigned long tmp;

    tmp = (hashval * (unsigned long)sb) ^ (GOLDEN_RATIO_NUM + hashval) /
            L1_CACHE_BYTES;
    tmp = tmp ^ ((tmp ^ GOLDEN_RATIO_NUM) >> i_hash_shift);
    return tmp & i_hash_mask;
}

int insert_inode(struct super_block *sb, struct inode *inode)
{

    return 0;
}

struct inode *lookup_inode(struct super_block *sb, unsigned long hashval)
{
    return NULL;
}

extern struct dentry *root_dentry;

static int name_len(const char *path, int n_pos)
{
    int i = 0;
    while (path[n_pos] != '/' && path[n_pos] != '\0') {
        i++; n_pos++;
    }
    return i;
}

struct dentry *lookup_path_from(struct dentry *parent, const char *path)
{
    int n_pos = 0;
    struct inode *inode;
    struct dentry *current = parent;
    struct dentry *find;

    while (path[n_pos] != '\0') {
        if (path[n_pos] == '/') {
            n_pos++;
            continue;
        }
        int len = name_len(path, n_pos);
        if (len == 0)
            break;
        char *name = kzmalloc(len+1);
        strncpy(name, path + n_pos, len);
        name[len] = '\0';

        klog(LOG_DEBUG, "VFS: Looking up %s\n", name);
        find = dlookup(current, name);
        if (find == NULL) {
            klog(LOG_DEBUG, "VFS: %s not in cache\n", name);

            find = kzmalloc(sizeof(*find));

            inode = current->d_inode;
            find->d_parent = current;
            find->d_name = name;
            find->d_sb = inode->i_sb;

            inode->i_op->lookup(inode, find, 0);

            dcache_add(find);

            // If the inode is NULL, we've reached a dead end (negative dentry)
            if (find->d_inode == NULL)
                return find;
        }
        else {
            kfree(name);
            if (find->d_inode == NULL)
                return find;

            if (find->d_mount) {
                klog(LOG_DEBUG, "VFS: Found mount point\n");
                find = find->d_mount->mnt_root;
            }
        }
        current = find;
        n_pos += len;
    }

    klog(LOG_DEBUG, "VFS: Found %s\n", current->d_name);

    return current;
}

struct dentry *lookup_path(const char *path)
{
    if (strcmp(path, "/") == 0)
        return root_dentry;
    return lookup_path_from(root_dentry, path);
}
