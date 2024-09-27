#include <lilac/fs.h>
#include <lilac/log.h>
#include <string.h>

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
