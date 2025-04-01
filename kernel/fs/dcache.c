#include <lilac/fs.h>
#include <lilac/log.h>
#include <lilac/err.h>
#include <lilac/libc.h>
#include <mm/kmalloc.h>

#include "utils.h"

extern struct dentry *root_dentry;

struct dentry *dlookup(struct dentry *parent, const char *name)
{
    struct dentry *d = NULL;
    hlist_for_each_entry(d, &parent->d_children, d_sib) {
        if (strcmp(d->d_name, name) == 0)
            return d;
    }
    return NULL;
}

void dcache_add(struct dentry *d)
{
    hlist_add_head(&d->d_sib, &d->d_parent->d_children);
}

void dcache_remove(struct dentry *d)
{
    hlist_del(&d->d_sib);
}

void dget(struct dentry *d)
{
    d->d_count++;
}

void dput(struct dentry *d)
{
    //struct inode *i = d->d_inode;
    if (--d->d_count)
        return;

    /*
    if (hlist_empty(&d->d_children)) {
        dcache_remove(d);
        destroy_dentry(d);
    }

    if (i)
        iput(i);
    */
}

struct dentry * alloc_dentry(struct dentry *d_parent, const char *name)
{
    struct inode *i_parent = d_parent->d_inode;
    struct dentry *new_dentry = kzmalloc(sizeof(*new_dentry));
    if (!new_dentry)
        return ERR_PTR(-ENOMEM);

    new_dentry->d_parent = d_parent;
    new_dentry->d_sb = i_parent->i_sb;
    new_dentry->d_name = (char*)name;

    return new_dentry;
}

void destroy_dentry(struct dentry *d)
{
    kfree(d->d_name);
    kfree(d);
}

static char * next_path_component(const char *path, int *pos)
{
    while (path[*pos] == '/')
        (*pos)++;
    int len = path_component_len(path, *pos);
    if (len == 0)
        return NULL;
    char *name = kzmalloc(len+1);
    if (!name)
        return ERR_PTR(-ENOMEM);
    strncpy(name, path + *pos, len);
    name[len] = '\0';
    *pos += len;
    return name;
}

struct dentry * lookup_path_from(struct dentry *parent, const char *path)
{
    int n_pos = 0;
    struct inode *inode;
    struct dentry *find;
    char *name;

    while (path[n_pos] != '\0') {
        name = next_path_component(path, &n_pos);
        if (IS_ERR(name))
            return ERR_CAST(name);
        else if (!name)
            break;

        if (strcmp(name, ".") == 0) {
            kfree(name);
            continue;
        } else if (strcmp(name, "..") == 0) {
            kfree(name);
            if (parent->d_parent != NULL)
                parent = parent->d_parent;
            continue;
        }

        find = dlookup(parent, name);
        if (find == NULL) {
#ifdef DEBUG_VFS
            klog(LOG_DEBUG, "VFS: %s not in cache\n", name);
#endif
            find = alloc_dentry(parent, name);
            if (IS_ERR_OR_NULL(find)) {
                kfree(name);
                return find;
            }

            inode = parent->d_inode;
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
#ifdef DEBUG_VFS
                klog(LOG_DEBUG, "VFS: Found mount point\n");
#endif
                find = find->d_mount->mnt_root;
            }
        }
        parent = find;
    }
    dget(parent);
    return parent;
}

struct dentry * lookup_path(const char *path)
{
    if (strcmp(path, "/") == 0)
        return root_dentry;
    return lookup_path_from(root_dentry, path);
}
