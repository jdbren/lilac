#include <lilac/fs.h>
#include <lilac/log.h>
#include <lilac/panic.h>
#include <lilac/err.h>
#include <lilac/libc.h>
#include <lilac/sync.h>
#include <mm/kmalloc.h>

#include "utils.h"

#define VFS_MAX_SYMLINKS 20

extern struct dentry *root_dentry;

spinlock_t rename_lock = SPINLOCK_INIT;

/*
 * Reference counting rules
 *
 * d_count counts holders, and each holder owns exactly one reference:
 *  - the cache: a dentry on its parent's d_children list (dcache_add takes
 *    it, d_drop and d_prune_negative put it)
 *  - each child: a dentry holds its parent (alloc_dentry takes it, freeing
 *    the child or d_move puts it)
 *  - each caller of a lookup (lookup_path*, vfs_lookup*, get_parent_dentry,
 *    dlookup), which must dput the result
 *  - long-lived users: open files, cwd, mounts (the mountpoint and mnt_root)
 *
 * When the count reaches 0 the dentry is freed and puts its parent and its
 * inode. A cached dentry can't reach 0, since the cache holds a reference,
 * so a dentry is freed only after it is dropped from the cache and every
 * other holder has put it. Roots have no parent and are never freed.
 *
 * Inodes: each positive dentry holds one i_count reference on d_inode,
 * taken by the filesystem's lookup/create/mkdir/link/symlink.
 */

static bool dname_eq_nocase(const struct dentry *d, const char *name)
{
    size_t i;
    for (i = 0; i < d->d_name.len; i++) {
        if (!name[i] || toupper(d->d_name.data[i]) != toupper(name[i]))
            return false;
    }
    return name[i] == '\0';
}

struct dentry *dlookup(struct dentry *parent, char *name)
{
    struct dentry *d = NULL;
    bool nocase = parent->d_sb->s_type == MSDOS;
    hlist_for_each_entry(d, &parent->d_children, d_sib) {
        if (nocase ? dname_eq_nocase(d, name) : lstrcmp_cstr(&d->d_name, name) == 0) {
            dget(d);
            return d;
        }
    }
    return NULL;
}

void dcache_add(struct dentry *d)
{
    dget(d);
    hlist_add_head(&d->d_sib, &d->d_parent->d_children);
}

// Remove d from the cache so lookups no longer find it, and drop the cache's
// reference. It is freed once every other holder has put it.
void d_drop(struct dentry *d)
{
    struct dentry *parent = d->d_parent;
    bool hashed = false;

    if (parent) {
        mutex_lock(&parent->d_lock);
        if (!hlist_unhashed(&d->d_sib)) {
            hlist_del_init(&d->d_sib);
            hashed = true;
        }
        mutex_unlock(&parent->d_lock);
    }
    if (hashed)
        dput(d);
}

// Drop the cached negative children of dir
void d_prune_negative(struct dentry *dir)
{
    struct dentry *child;
    struct hlist_node *tmp;

    mutex_lock(&dir->d_lock);
    hlist_for_each_entry_safe(child, tmp, &dir->d_children, d_sib) {
        if (!child->d_inode) {
            hlist_del_init(&child->d_sib);
            dput(child);
        }
    }
    mutex_unlock(&dir->d_lock);
}

/*
 * Move a cached dentry to new_parent under new_name (after a successful
 * rename)
 */
void d_move(struct dentry *d, struct dentry *new_parent, char *new_name)
{
    struct dentry *old_parent = d->d_parent;
    char *old_name;

    mutex_lock(&old_parent->d_lock);
    hlist_del_init(&d->d_sib);
    mutex_unlock(&old_parent->d_lock);

    dget(new_parent);

    acquire_lock(&rename_lock);
    old_name = d->d_name.data;
    d->d_name.data = new_name;
    d->d_name.len = strlen(new_name);
    d->d_parent = new_parent;
    release_lock(&rename_lock);
    kfree(old_name);

    mutex_lock(&new_parent->d_lock);
    hlist_add_head(&d->d_sib, &new_parent->d_children);
    mutex_unlock(&new_parent->d_lock);

    dput(old_parent);
}

void dget(struct dentry *d)
{
    atomic_fetch_add(&d->d_count, 1);
}

// d's current parent, referenced
struct dentry * dget_parent(struct dentry *d)
{
    acquire_lock(&rename_lock);
    struct dentry *parent = d->d_parent;
    if (parent)
        dget(parent);
    release_lock(&rename_lock);
    return parent;
}

struct dentry * lock_parent(struct dentry *d)
{
    if (d_unhashed(d))
        return d->d_parent ? ERR_PTR(-ENOENT) : ERR_PTR(-EBUSY);
    for (;;) {
        struct dentry *parent = dget_parent(d);
        if (!parent)
            return ERR_PTR(-EBUSY);
        inode_lock(parent->d_inode);
        if (d->d_parent == parent && !d_unhashed(d))
            return parent;
        bool moved = d->d_parent != parent;
        inode_unlock(parent->d_inode);
        dput(parent);
        if (!moved)
            return NULL;
    }
}

void unlock_parent(struct dentry *parent)
{
    inode_unlock(parent->d_inode);
    dput(parent);
}

// Is anc a proper ancestor of d? The tree must be held still (rename_mutex).
bool d_is_ancestor(struct dentry *anc, struct dentry *d)
{
    for (d = d->d_parent; d; d = d->d_parent) {
        if (d == anc)
            return true;
    }
    return false;
}

void dput(struct dentry *d)
{
    while (d) {
        unsigned int old = atomic_fetch_sub(&d->d_count, 1);
        if (old == 0)
            panic("dput: dentry %p (%s) count underflow\n", d, d->d_name.data);
        if (old > 1)
            return;
        if (!hlist_unhashed(&d->d_sib) || !d->d_parent) {
            klog(LOG_ERROR, "dput: freeing live dentry %p (%s)\n", d, d->d_name.data);
            return;
        }

        struct dentry *parent = d->d_parent;
        struct inode *inode = d->d_inode;
        destroy_dentry(d);
        if (inode)
            iput(inode);
        d = parent;
    }
}

struct dentry * alloc_dentry(struct dentry *d_parent, const char *name)
{
    struct inode *i_parent = d_parent->d_inode;
    struct dentry *new_dentry = kzmalloc(sizeof(*new_dentry));
    if (!new_dentry)
        return ERR_PTR(-ENOMEM);

    dget(d_parent);
    new_dentry->d_parent = d_parent;
    new_dentry->d_sb = i_parent->i_sb;
    new_dentry->d_name.data = strdup(name);
    new_dentry->d_name.len = strlen(name);
    mutex_init(&new_dentry->d_lock);
    INIT_HLIST_HEAD(&new_dentry->d_children);
    INIT_HLIST_NODE(&new_dentry->d_sib);
    new_dentry->d_count = 1;

    return new_dentry;
}

// Frees d itself; the caller puts its parent and inode (see dput)
void destroy_dentry(struct dentry *d)
{
    char *name = d->d_name.data;
#ifdef DEBUG_VFS
    // poison, so a use after free shows up as garbage instead of stale data
    if (name)
        memset(name, 0x6b, d->d_name.len);
    memset(d, 0x6b, sizeof(*d));
#endif
    kfree(name);
    kfree(d);
}

static char * next_path_component(const char *path, int *pos)
{
    while (path[*pos] == '/')
        (*pos)++;
    int len = path_component_len(path, *pos);
    if (len == 0)
        return NULL;
    if (len > DNAME_MAX)
        return ERR_PTR(-ENAMETOOLONG);
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

    struct dentry *base = expanded[0] == '/' ? root_dentry : dget_parent(find);
    struct dentry *resolved = lookup_path_from_inner(base, expanded,
        follow_final, link_count + 1);
    if (base != root_dentry)
        dput(base);
    kfree(expanded);
    return resolved;
}

// Ask the filesystem for name under parent and cache the result (possibly
// negative). The caller holds the directory's i_mutex.
static struct dentry * lookup_child_slow(struct dentry *parent, char *name)
{
    struct inode *dir = parent->d_inode;
    struct dentry *find, *res;

    mutex_lock(&parent->d_lock);
    find = dlookup(parent, name);
    if (find)
        goto out;
    if (IS_DEADDIR(dir)) {
        find = ERR_PTR(-ENOENT);
        goto out;
    }
    find = alloc_dentry(parent, name);
    if (IS_ERR(find))
        goto out;
    res = dir->i_op->lookup(dir, find, 0);
    if (IS_ERR(res)) {
        dput(find);
        find = res;
        goto out;
    }
    if (res && res != find) {
        dput(find);
        find = res;
        goto out;
    }
    dcache_add(find);
out:
    mutex_unlock(&parent->d_lock);
    return find;
}

// Find name under parent in the cache, or ask the filesystem and cache the
// result (possibly negative). Returns a referenced dentry.
static struct dentry * lookup_child(struct dentry *parent, char *name)
{
    struct inode *dir = parent->d_inode;
    struct dentry *find, *res;

    mutex_lock(&parent->d_lock);
    find = dlookup(parent, name);
    mutex_unlock(&parent->d_lock);
    if (!find) {
        if (!S_ISDIR(dir->i_mode))
            return ERR_PTR(-ENOTDIR);
        inode_lock_shared(dir);
        find = lookup_child_slow(parent, name);
        inode_unlock_shared(dir);
        if (IS_ERR(find))
            return find;
    }
    if (find->d_inode && find->d_mount) {
        res = find->d_mount->mnt_root;
        dget(res);
        dput(find);
        find = res;
    }
    return find;
}

static struct dentry * lookup_path_from_inner(struct dentry *parent,
    const char *path, int follow_final, unsigned int link_count)
{
    int n_pos = 0;
    struct dentry *find;
    char *name;

    // the walk holds a reference on its current position
    dget(parent);
    while ((name = next_path_component(path, &n_pos)) != NULL) {
        if (IS_ERR(name)) {
            find = ERR_CAST(name);
            goto out;
        }
#ifdef DEBUG_VFS
        klog(LOG_DEBUG, "VFS: Looking up %s\n", name);
#endif
        if (strcmp(name, ".") == 0) {
            kfree(name);
            continue;
        }
        if (strcmp(name, "..") == 0) {
            kfree(name);
            find = dget_parent(parent);
            if (find) {
                dput(parent);
                parent = find;
            }
            continue;
        }

        find = lookup_child(parent, name);
        kfree(name);
        if (IS_ERR(find))
            goto out;

        // a negative dentry ends the walk
        if (!find->d_inode) {
            if (path_has_component(path, n_pos)) {
                dput(find);
                find = ERR_PTR(-ENOENT);
            }
            goto out;
        }

        if (S_ISLNK(find->d_inode->i_mode) &&
           (follow_final || path_has_component(path, n_pos))) {
            struct dentry *res = resolve_symlink(find, path, n_pos, follow_final, link_count);
            dput(find);
            find = res;
            goto out;
        }

        dput(parent);
        parent = find;
    }

    size_t len = strlen(path);
    if (len > 0 && path[len - 1] == '/' && parent->d_inode &&
            !S_ISDIR(parent->d_inode->i_mode)) {
        find = ERR_PTR(-ENOTDIR);
        goto out;
    }
    return parent;
out:
    dput(parent);
    return find;
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

// Returns a referenced dentry; the caller must dput it
struct dentry * lookup_path(const char *path)
{
    return lookup_path_from(root_dentry, path);
}
