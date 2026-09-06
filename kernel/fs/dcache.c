#include <lilac/fs.h>
#include <lilac/log.h>
#include <lilac/err.h>
#include <lilac/libc.h>
#include <lilac/sync.h>
#include <mm/kmalloc.h>

#include "utils.h"

#define VFS_MAX_SYMLINKS 20

extern struct dentry *root_dentry;

struct dentry *dlookup(struct dentry *parent, char *name)
{
    struct dentry *d = NULL;
    if (parent->d_sb->s_type == MSDOS) {
        for (int i = 0; name[i]; i++)
            name[i] = toupper(name[i]);
    }
    hlist_for_each_entry(d, &parent->d_children, d_sib) {
        if (lstrcmp_cstr(&d->d_name, name) == 0) {
            dget(d);
            return d;
        }
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

    klog(LOG_WARN, "dput: dentry %p (%s) count is zero, but not freeing it yet\n", d, d->d_name);
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
    new_dentry->d_name.data = strdup(name);
    new_dentry->d_name.len = strlen(name);
    spin_lock_init(&new_dentry->d_lock);
    INIT_HLIST_HEAD(&new_dentry->d_children);
    INIT_HLIST_NODE(&new_dentry->d_sib);
    new_dentry->d_count = 1;

    return new_dentry;
}

void destroy_dentry(struct dentry *d)
{
    kfree(d->d_name.data);
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

static int path_has_component(const char *path, int pos)
{
    while (path[pos] == '/')
        pos++;
    return path[pos] != '\0';
}

static struct dentry * lookup_path_from_inner(struct dentry *parent,
    const char *path, int follow_final, unsigned int link_count);

static struct dentry * resolve_symlink(struct dentry *find, const char *path,
    int n_pos, int follow_final, unsigned int link_count)
{
    if (!find->d_inode->i_op->get_link)
        return ERR_PTR(-EIO);

    const char *target = find->d_inode->i_op->get_link(find, find->d_inode);
    if (IS_ERR(target))
        return ERR_CAST(target);
    if (!target)
        return ERR_PTR(-EIO);
    if (link_count >= VFS_MAX_SYMLINKS)
        return ERR_PTR(-ELOOP);

    size_t target_len = strlen(target);
    size_t suffix_pos = (size_t)n_pos;
    size_t suffix_len = strlen(path + suffix_pos);
    if (target_len > PATH_MAX - 2 ||
        suffix_len > PATH_MAX - target_len - 2)
        return ERR_PTR(-ENAMETOOLONG);

    char *expanded = kmalloc(target_len + suffix_len + 2);
    if (!expanded)
        return ERR_PTR(-ENOMEM);
    memcpy(expanded, target, target_len);
    if (suffix_len > 0) {
        if (target_len > 0 && expanded[target_len - 1] != '/')
            expanded[target_len++] = '/';
        memcpy(expanded + target_len, path + suffix_pos, suffix_len);
        target_len += suffix_len;
    }
    expanded[target_len] = '\0';

    struct dentry *base = expanded[0] == '/' ? root_dentry : find->d_parent;
    struct dentry *resolved = lookup_path_from_inner(base, expanded,
        follow_final, link_count + 1);
    kfree(expanded);
    return resolved;
}

static struct dentry * lookup_path_from_inner(struct dentry *parent,
    const char *path, int follow_final, unsigned int link_count)
{
    int n_pos = 0;
    struct inode *inode;
    struct dentry *find, *tmp;
    char *name;
    long err = 0;

    while (path[n_pos] != '\0') {
        name = next_path_component(path, &n_pos);
        if (IS_ERR(name))
            return ERR_CAST(name);
        else if (!name)
            break;
#ifdef DEBUG_VFS
        klog(LOG_DEBUG, "VFS: Looking up %s\n", name);
#endif
        acquire_lock(&parent->d_lock);

        if (strcmp(name, ".") == 0) {
            kfree(name);
            release_lock(&parent->d_lock);
            continue;
        } else if (strcmp(name, "..") == 0) {
            kfree(name);
            tmp = parent;
            if (parent->d_parent != NULL)
                parent = parent->d_parent;
            release_lock(&tmp->d_lock);
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
                release_lock(&parent->d_lock);
                return find;
            }

            inode = parent->d_inode;
            if (!S_ISDIR(inode->i_mode)) {
                klog(LOG_DEBUG, "VFS: %s is not a directory\n", parent->d_name);
                kfree(name);
                destroy_dentry(find);
                release_lock(&parent->d_lock);
                return ERR_PTR(-ENOTDIR);
            }

            if ((err = PTR_ERR(inode->i_op->lookup(inode, find, 0))) < 0) {
                kfree(name);
                destroy_dentry(find);
                release_lock(&parent->d_lock);
                return ERR_PTR(err);
            }

            dcache_add(find);
            kfree(name);
            // If the inode is NULL, we've reached a dead end (negative dentry)
            if (find->d_inode == NULL) {
                release_lock(&parent->d_lock);
                return find;
            }
        } else {
#ifdef DEBUG_VFS
            klog(LOG_DEBUG, "VFS: Found %s in cache\n", name);
#endif
            kfree(name);
            if (find->d_inode == NULL) {
                release_lock(&parent->d_lock);
                return find;
            }

            if (find->d_mount) {
#ifdef DEBUG_VFS
                klog(LOG_DEBUG, "VFS: Found mount point\n");
#endif
                find = find->d_mount->mnt_root;
            }
        }
        release_lock(&parent->d_lock);

        if (find->d_inode && S_ISLNK(find->d_inode->i_mode) &&
           (follow_final || path_has_component(path, n_pos))) {
            return resolve_symlink(find, path, n_pos, follow_final, link_count);
        }

        parent = find;
    }
    return parent;
}

struct dentry * lookup_path_from_flags(struct dentry *parent, const char *path,
    int follow_final)
{
    return lookup_path_from_inner(parent, path, follow_final, 0);
}

struct dentry * lookup_path_from(struct dentry *parent, const char *path)
{
    return lookup_path_from_flags(parent, path, 1);
}

struct dentry * lookup_path(const char *path)
{
    if (strcmp(path, "/") == 0)
        return root_dentry;
    return lookup_path_from(root_dentry, path);
}
