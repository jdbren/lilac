// Directory entry management for FAT32
#include <fs/fat32.h>

#include <lilac/fs.h>
#include <lilac/log.h>
#include <lilac/libc.h>
#include <lilac/timer.h>
#include <lilac/err.h>
#include <drivers/blkdev.h>
#include <mm/kmalloc.h>

#include "fat_internal.h"

#define FAT_EOC         0x0FFFFFF8U
#define LFN_CHARS       13
#define LFN_LAST        0x40

static inline u32 dir_nents(const struct fat_dir *d)
{
    return d->size / sizeof(struct fat_file);
}

struct fat_file *fat_dir_entry(struct fat_dir *d, u32 idx)
{
    return (struct fat_file*)d->buf + idx;
}

static void dir_mark_dirty(struct fat_dir *d, u32 first, u32 last)
{
    if (first < d->dirty_lo)
        d->dirty_lo = first;
    if (last + 1 > d->dirty_hi)
        d->dirty_hi = last + 1;
}

static inline u32 dir_first_clst(struct inode *dir)
{
    return fat_clst_value((struct fat_file*)dir->i_private);
}

int fat_dir_load(struct inode *dir, struct fat_dir *d)
{
    volatile u8 *buf = NULL;

    d->disk = (struct fat_disk*)dir->i_sb->s_fs_info;
    d->hd = dir->i_sb->s_bdev->disk;
    d->first_clst = dir_first_clst(dir);
    d->buf = NULL;
    d->size = 0;
    d->dirty_lo = (u32)-1;
    d->dirty_hi = 0;
    d->fat_dirty = false;

    ssize_t n = __fat32_read_dir(d->disk, &buf, d->first_clst);
    if (n <= 0) {
        kfree((void*)buf);
        return n < 0 ? (int)n : -EIO;
    }
    d->buf = (u8*)buf;
    d->size = n;
    return 0;
}

void fat_dir_put(struct fat_dir *d)
{
    kfree(d->buf);
    d->buf = NULL;
}

// Write back every cluster holding a modified entry, then the FAT if the
// directory grew. Returns the first error; everything is still attempted.
int fat_dir_flush(struct fat_dir *d)
{
    const u32 bpc = d->disk->bytes_per_clst;
    const u32 per_clst = bpc / sizeof(struct fat_file);
    int err = 0;

    if (d->dirty_lo < d->dirty_hi) {
        u32 lo = d->dirty_lo / per_clst;
        u32 hi = (d->dirty_hi - 1) / per_clst;
        u32 clst = d->first_clst;
        for (u32 i = 0; i <= hi && clst < FAT_EOC; i++) {
            if (i >= lo) {
                int e = __fat_write_clst(d->disk, d->hd, clst, d->buf + i * bpc);
                if (e && !err)
                    err = e;
            }
            clst = fat_value(clst, d->disk);
        }
    }
    d->dirty_lo = (u32)-1;
    d->dirty_hi = 0;

    if (d->fat_dirty) {
        d->fat_dirty = false;
        int e = fat_write_FAT(d->disk, d->hd);
        if (e < 0 && !err)
            err = e;
    }
    return err;
}

static void fat_entry_sfn(const struct fat_file *e, unsigned char sfn[11])
{
    memcpy(sfn, e->name, 8);
    memcpy(sfn + 8, e->ext, 3);
}

static u8 fat_sfn_checksum(const unsigned char sfn[11])
{
    u8 sum = 0;
    for (int i = 0; i < 11; i++)
        sum = ((sum & 1) << 7) + (sum >> 1) + sfn[i];
    return sum;
}

// Advance pos to the next live short entry, collecting the long name that
// belongs to it. Returns false at the end of the directory.
bool fat_dir_next(struct fat_dir *d, struct fat_dir_pos *pos)
{
    bool have_lfn = false;
    u32 lfn_first = 0;
    u8 csum = 0;
    unsigned char sfn[11];

    memset(pos->lfn, 0, sizeof(pos->lfn));

    for (u32 i = pos->next; i < dir_nents(d); i++) {
        struct fat_file *e = fat_dir_entry(d, i);

        if (e->name[0] == 0)
            break;
        if (e->name[0] == FAT_UNUSED) {
            have_lfn = false;
            continue;
        }

        if (e->attributes == LONG_FNAME) {
            struct fat_lfn *l = (struct fat_lfn*)e;
            if (l->order & LFN_LAST) {
                memset(pos->lfn, 0, sizeof(pos->lfn));
                have_lfn = true;
                lfn_first = i;
                csum = l->checksum;
            } else if (l->checksum != csum) {
                have_lfn = false;
            }
            if (have_lfn)
                fat_get_lfn_part(e, pos->lfn);
            continue;
        }

        if (INVALID_ENTRY(e)) {
            have_lfn = false;
            continue;
        }

        fat_entry_sfn(e, sfn);
        if (have_lfn && fat_sfn_checksum(sfn) == csum) {
            pos->first = lfn_first;
        } else {
            pos->first = i;
            memset(pos->lfn, 0, sizeof(pos->lfn));
        }
        pos->sfn = i;
        pos->next = i + 1;
        return true;
    }

    pos->next = dir_nents(d);
    return false;
}

bool fat_dir_find(struct fat_dir *d, const char *name, struct fat_dir_pos *pos)
{
    char sfn[13];

    pos->next = 0;
    while (fat_dir_next(d, pos)) {
        if (pos->lfn[0] && !fat_strcasecmp(pos->lfn, name))
            return true;
        fat_get_sfn(fat_dir_entry(d, pos->sfn), sfn);
        if (!fat_strcasecmp(sfn, name))
            return true;
    }
    return false;
}

// True if the directory holds nothing but "." and ".."
bool fat_dir_empty(struct fat_dir *d)
{
    struct fat_dir_pos pos = { .next = 0 };
    while (fat_dir_next(d, &pos))
        if (fat_dir_entry(d, pos.sfn)->name[0] != '.')
            return false;
    return true;
}

static bool fat_dir_sfn_taken(struct fat_dir *d, const unsigned char sfn[11])
{
    struct fat_dir_pos pos = { .next = 0 };
    unsigned char cur[11];
    while (fat_dir_next(d, &pos)) {
        fat_entry_sfn(fat_dir_entry(d, pos.sfn), cur);
        if (!memcmp(cur, sfn, 11))
            return true;
    }
    return false;
}

// Mark a name (its long entries and its short entry) as deleted
void fat_dir_remove(struct fat_dir *d, u32 first, u32 sfn)
{
    for (u32 i = first; i <= sfn; i++)
        fat_dir_entry(d, i)->name[0] = FAT_UNUSED;
    dir_mark_dirty(d, first, sfn);
}

// Append a zeroed cluster to the directory
static int fat_dir_extend(struct fat_dir *d)
{
    const u32 bpc = d->disk->bytes_per_clst;
    u32 last = d->first_clst;
    while (fat_value(last, d->disk) < FAT_EOC)
        last = fat_value(last, d->disk);

    u8 *buf = krealloc(d->buf, d->size + bpc);
    if (!buf)
        return -ENOMEM;
    d->buf = buf;

    if (__fat_find_alloc_clst(d->disk, last) == 0)
        return -ENOSPC;
    d->fat_dirty = true;

    memset(d->buf + d->size, 0, bpc);
    u32 first_new = dir_nents(d);
    d->size += bpc;
    dir_mark_dirty(d, first_new, dir_nents(d) - 1);
    return 0;
}

// Find `need` consecutive free entries, growing the directory if necessary
static int fat_dir_reserve(struct fat_dir *d, u32 need, u32 *start)
{
    u32 run = 0;
    u32 i = 0;

    while (1) {
        for (; i < dir_nents(d); i++) {
            u8 c = fat_dir_entry(d, i)->name[0];
            if (c != 0 && c != FAT_UNUSED) {
                run = 0;
                continue;
            }
            if (++run == need) {
                *start = i + 1 - need;
                return 0;
            }
        }
        int err = fat_dir_extend(d);
        if (err)
            return err;
    }
}

static bool fat_sfn_char(char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        return true;
    return c && strchr("$%'-_@~`!(){}^#&", c) != NULL;
}

// Whether name can be stored as a plain 8.3 entry: one optional dot, base of
// 1-8 and extension of 1-3 valid characters, and each part in a single case
// (the NT case flags can then restore it exactly)
static bool fat_name_fits_sfn(const char *name)
{
    const char *dot = strchr(name, '.');
    if (dot == name || (dot && strchr(dot + 1, '.')))
        return false;

    size_t base_len = dot ? (size_t)(dot - name) : strlen(name);
    size_t ext_len = dot ? strlen(dot + 1) : 0;
    if (base_len == 0 || base_len > 8 || ext_len > 3 || (dot && ext_len == 0))
        return false;

    bool lower[2] = { false, false }, upper[2] = { false, false };
    for (const char *p = name; *p; p++) {
        if (p == dot)
            continue;
        if (!fat_sfn_char(*p))
            return false;
        int part = dot && p > dot;
        lower[part] |= islower(*p);
        upper[part] |= isupper(*p);
    }
    return !(lower[0] && upper[0]) && !(lower[1] && upper[1]);
}

static char fat_sfn_upper(char c)
{
    return fat_sfn_char(c) ? toupper(c) : '_';
}

// Aliases go up to ~99999, which still leaves the basis 2 characters
#define FAT_ALIAS_MAX 99999

// If e is an alias "BASIS~N.EXT" of basis (base_len characters before the
// tail, cut to fit), return N; otherwise 0
static u32 fat_alias_number(const struct fat_file *e, const unsigned char basis[11],
                            size_t base_len)
{
    if (memcmp(e->ext, basis + 8, 3))
        return 0;
    // the basis may itself contain '~': the tail starts at the last one
    size_t at = 8, len = 1;
    for (size_t i = 0; i < 8; i++)
        if (e->name[i] == '~')
            at = i;
    if (at == 8)
        return 0;
    u32 n = 0;
    while (at + len < 8 && e->name[at + len] >= '0' && e->name[at + len] <= '9') {
        n = n * 10 + (e->name[at + len] - '0');
        len++;
    }
    for (size_t i = at + len; i < 8; i++)
        if (e->name[i] != ' ')
            return 0;
    if (len == 1 || e->name[at + 1] == '0' || n > FAT_ALIAS_MAX)
        return 0;
    if (at != MIN(base_len, 8 - len) || memcmp(e->name, basis, at))
        return 0;
    return n;
}

// Generate a unique "BASIS~N.EXT" short alias for a name that needs a long entry
static int fat_make_alias(struct fat_dir *d, const char *name, unsigned char sfn[11])
{
    unsigned char basis[11];
    size_t base_len = 0;

    while (*name == '.' || *name == ' ')
        name++;
    const char *dot = strrchr(name, '.');
    const char *end = dot ? dot : name + strlen(name);

    memset(basis, ' ', sizeof(basis));
    for (const char *p = name; p < end && base_len < 8; p++) {
        if (*p == ' ' || *p == '.')
            continue;
        basis[base_len++] = fat_sfn_upper(*p);
    }
    if (base_len == 0)
        basis[base_len++] = '_';
    if (dot) {
        size_t j = 8;
        for (const char *p = dot + 1; *p && j < 11; p++) {
            if (*p == ' ')
                continue;
            basis[j++] = fat_sfn_upper(*p);
        }
    }

    // One pass over the directory marks which ~N are taken for this basis
    u8 *taken = kzmalloc(FAT_ALIAS_MAX / 8 + 1);
    if (!taken)
        return -ENOMEM;
    struct fat_dir_pos pos = { .next = 0 };
    while (fat_dir_next(d, &pos)) {
        u32 n = fat_alias_number(fat_dir_entry(d, pos.sfn), basis, base_len);
        if (n)
            taken[n / 8] |= 1 << (n % 8);
    }

    u32 n = 1;
    while (n <= FAT_ALIAS_MAX && (taken[n / 8] & (1 << (n % 8))))
        n++;
    kfree(taken);
    if (n > FAT_ALIAS_MAX)
        return -EEXIST;

    char tail[8];
    size_t tail_len = snprintf(tail, sizeof(tail), "~%u", n);
    size_t keep = MIN(base_len, 8 - tail_len);
    memcpy(sfn, basis, 11);
    memcpy(sfn + keep, tail, tail_len);
    memset(sfn + keep + tail_len, ' ', 8 - keep - tail_len);
    return 0;
}

// Fill long entry `ord` (1-based) of name; characters are stored as UCS-2,
// terminated by a NUL and padded with 0xFFFF
static void fat_fill_lfn(struct fat_lfn *l, const char *name, size_t len,
                         int ord, bool last, u8 csum)
{
    for (int i = 0; i < LFN_CHARS; i++) {
        size_t at = (size_t)(ord - 1) * LFN_CHARS + i;
        u16 c = at < len ? (u8)name[at] : at == len ? 0 : 0xFFFF;
        u8 *dst = i < 5  ? &l->name1[i * 2] :
                  i < 11 ? &l->name2[(i - 5) * 2] :
                           &l->name3[(i - 11) * 2];
        dst[0] = c & 0xFF;
        dst[1] = c >> 8;
    }
    l->order = ord | (last ? LFN_LAST : 0);
    l->attr = LONG_FNAME;
    l->type = 0;
    l->checksum = csum;
    l->cluster = 0;
}

// Add name to the directory. entry supplies everything but the name, and
// gets the short name that was chosen; idx gets the short entry's index.
int fat_dir_add(struct fat_dir *d, const char *name, struct fat_file *entry, u32 *idx)
{
    size_t len = strlen(name);
    unsigned char sfn[11];
    u8 case_flags = 0;
    u32 nlfn = 0;
    u32 start;
    int err;

    if (len == 0)
        return -ENOENT;
    if (len > FAT_LFN_MAX)
        return -ENAMETOOLONG;

    if (fat_name_fits_sfn(name)) {
        case_flags = fat_make_sfn(name, sfn);
        if (fat_dir_sfn_taken(d, sfn))
            return -EEXIST;
    } else {
        if ((err = fat_make_alias(d, name, sfn)))
            return err;
        nlfn = (len + LFN_CHARS - 1) / LFN_CHARS;
    }

    if ((err = fat_dir_reserve(d, nlfn + 1, &start)))
        return err;

    u8 csum = fat_sfn_checksum(sfn);
    // long entries are stored in reverse, the last part first
    for (u32 k = 0; k < nlfn; k++) {
        int ord = nlfn - k;
        fat_fill_lfn((struct fat_lfn*)fat_dir_entry(d, start + k), name, len,
                     ord, k == 0, csum);
    }

    memcpy(entry->name, sfn, 8);
    memcpy(entry->ext, sfn + 8, 3);
    entry->reserved = case_flags;
    *fat_dir_entry(d, start + nlfn) = *entry;
    dir_mark_dirty(d, start, start + nlfn);
    *idx = start + nlfn;
    return 0;
}

static void fat_init_entry(struct fat_file *e, u8 attr, u32 clst)
{
    struct timestamp now = get_timestamp();
    u16 date = FAT_SET_DATE(now.year, now.month, now.day);
    u16 time = FAT_SET_TIME(now.hour, now.minute, now.second);

    memset(e, 0, sizeof(*e));
    memset(e->name, ' ', 8);
    memset(e->ext, ' ', 3);
    e->attributes = attr;
    e->cl_low = clst & 0xFFFF;
    e->cl_high = clst >> 16;
    e->creation_date = date;
    e->creation_time = time;
    e->last_write_date = date;
    e->last_write_time = time;
    e->last_access_date = date;
}

// The cluster a ".." entry records for dir: 0 stands for the root
static u32 fat_parent_ref(struct inode *dir)
{
    struct fat_disk *disk = (struct fat_disk*)dir->i_sb->s_fs_info;
    u32 clst = dir_first_clst(dir);
    return clst == disk->root_start ? 0 : clst;
}

// Create a file (attr 0) or directory (FAT_DIR_ATTR) named after dentry
int fat_new_entry(struct inode *dir, struct dentry *dentry, u8 attr)
{
    struct fat_disk *disk = (struct fat_disk*)dir->i_sb->s_fs_info;
    struct gendisk *hd = dir->i_sb->s_bdev->disk;
    const char *name = dentry->d_name.data;
    struct fat_dir d;
    struct fat_dir_pos pos;
    struct inode *inode;
    u8 *data = NULL;
    u32 clst = 0;
    int err;

    struct fat_inode *fi = kzmalloc(sizeof(*fi));
    if (!fi)
        return -ENOMEM;

    if ((err = fat_dir_load(dir, &d)))
        goto out_free;
    if (fat_dir_find(&d, name, &pos)) {
        err = -EEXIST;
        goto out_put;
    }

    // An empty file has no clusters; a directory needs one for "." and ".."
    if (attr & FAT_DIR_ATTR) {
        data = kzmalloc(disk->bytes_per_clst);
        if (!data) {
            err = -ENOMEM;
            goto out_put;
        }
        clst = __fat_find_alloc_clst(disk, 0);
        if (clst == 0) {
            err = -ENOSPC;
            goto out_put;
        }
    }

    fat_init_entry(&fi->entry, attr, clst);
    if (attr & FAT_DIR_ATTR) {
        struct fat_file *dots = (struct fat_file*)data;
        fat_init_entry(&dots[0], FAT_DIR_ATTR, clst);
        dots[0].name[0] = '.';
        u32 parent = fat_parent_ref(dir);
        fat_init_entry(&dots[1], FAT_DIR_ATTR, parent);
        dots[1].name[0] = '.';
        dots[1].name[1] = '.';
        err = __fat_write_clst(disk, hd, clst, data);
    }

    if (err || (err = fat_dir_add(&d, name, &fi->entry, &fi->dir_idx))) {
        // write out any cluster the directory grew by before failing, since
        // the FAT now links it in
        fat_dir_flush(&d);
        fat_free_chain(disk, clst);
        (void)fat_write_FAT(disk, hd);
        goto out_put;
    }
    fi->dir_clst = d.first_clst;
    // If this fails the entry may or may not be on disk, so the new
    // directory's cluster stays allocated: a leaked cluster is safer than an
    // entry pointing at a free one
    if ((err = fat_dir_flush(&d)))
        goto out_put;
    if ((err = fat_write_FAT(disk, hd)) < 0)
        goto out_put;

    inode = fat_build_inode(dir->i_sb, fi);
    if (IS_ERR(inode)) {
        err = PTR_ERR(inode);
        goto out_put;
    }
    dentry->d_inode = inode;
    fi = NULL;

out_put:
    kfree(data);
    fat_dir_put(&d);
out_free:
    kfree(fi);
    return err;
}

// Give back the clusters of an inode that no directory entry refers to anymore
void fat_release_clusters(struct inode *inode)
{
    struct fat_inode *fi = (struct fat_inode*)inode->i_private;
    struct fat_disk *disk = (struct fat_disk*)inode->i_sb->s_fs_info;
    u32 clst = fat_clst_value(&fi->entry);

    if (clst >= 2) {
        fat_free_chain(disk, clst);
        if (fat_write_FAT(disk, inode->i_sb->s_bdev->disk) < 0)
            klog(LOG_WARN, "fat: failed to write the FAT after freeing clusters\n");
    }
    fi->entry.cl_low = 0;
    fi->entry.cl_high = 0;
}

// The inode's last name is gone. Its data stays readable through open files
// and is freed when the inode is destroyed (fat_destroy_inode).
static void fat_drop_inode(struct inode *inode)
{
    struct fat_inode *fi = (struct fat_inode*)inode->i_private;

    fi->unlinked = true;
    fi->dir_clst = 0;
    inode->i_nlink = 0;
    // no longer findable by cluster, which may now be reused
    acquire_lock(&inode->i_sb->s_lock);
    list_del_init(&inode->i_list);
    release_lock(&inode->i_sb->s_lock);
}

// Locate name, returning the entry range fat_dir_remove takes
static int fat_dir_find_slot(struct fat_dir *d, const char *name, u32 *first, u32 *sfn)
{
    struct fat_dir_pos pos;
    if (!fat_dir_find(d, name, &pos))
        return -ENOENT;
    *first = pos.first;
    *sfn = pos.sfn;
    return 0;
}

static int fat_remove_name(struct inode *dir, struct dentry *victim)
{
    struct fat_dir d;
    u32 first, sfn;
    int err;

    if ((err = fat_dir_load(dir, &d)))
        return err;
    if (!(err = fat_dir_find_slot(&d, victim->d_name.data, &first, &sfn))) {
        fat_dir_remove(&d, first, sfn);
        err = fat_dir_flush(&d);
    }
    fat_dir_put(&d);
    return err;
}

static int fat_check_dir_empty(struct inode *inode)
{
    struct fat_dir d;
    int err;

    if ((err = fat_dir_load(inode, &d)))
        return err;
    if (!fat_dir_empty(&d))
        err = -ENOTEMPTY;
    fat_dir_put(&d);
    return err;
}

int fat32_unlink(struct inode *dir, struct dentry *victim)
{
    int err = fat_remove_name(dir, victim);
    if (!err)
        fat_drop_inode(victim->d_inode);
    return err;
}

int fat32_rmdir(struct inode *dir, struct dentry *victim)
{
    int err = fat_check_dir_empty(victim->d_inode);
    if (!err && !(err = fat_remove_name(dir, victim)))
        fat_drop_inode(victim->d_inode);
    return err;
}

// Point the ".." entry of a moved directory at its new parent
static int fat_set_dotdot(struct inode *inode, struct inode *new_parent)
{
    struct fat_disk *disk = (struct fat_disk*)inode->i_sb->s_fs_info;
    struct gendisk *hd = inode->i_sb->s_bdev->disk;
    u32 clst = dir_first_clst(inode);
    struct fat_file *buf = kmalloc(disk->bytes_per_clst);
    if (!buf)
        return -ENOMEM;

    int err = __fat_read_clst(disk, hd, clst, buf);
    if (!err && buf[1].name[0] == '.' && buf[1].name[1] == '.') {
        u32 parent = fat_parent_ref(new_parent);
        buf[1].cl_low = parent & 0xFFFF;
        buf[1].cl_high = parent >> 16;
        err = __fat_write_clst(disk, hd, clst, buf);
    }
    kfree(buf);
    return err;
}

int fat32_rename(struct inode *old_dir, struct dentry *old_d,
                 struct inode *new_dir, struct dentry *new_d)
{
    struct inode *inode = old_d->d_inode;
    struct inode *target = new_d->d_inode;
    struct fat_inode *fi = (struct fat_inode*)inode->i_private;
    struct fat_dir od, nd_buf;
    struct fat_dir *nd = &od;
    u32 old_first, old_sfn, first, sfn, new_idx;
    int err;

    if (target && S_ISDIR(target->i_mode) && (err = fat_check_dir_empty(target)))
        return err;

    if ((err = fat_dir_load(old_dir, &od)))
        return err;
    if (new_dir != old_dir) {
        if ((err = fat_dir_load(new_dir, &nd_buf)))
            goto out_old;
        nd = &nd_buf;
    }

    if ((err = fat_dir_find_slot(&od, old_d->d_name.data, &old_first, &old_sfn)))
        goto out;
    if (target) {
        if ((err = fat_dir_find_slot(nd, new_d->d_name.data, &first, &sfn)))
            goto out;
        fat_dir_remove(nd, first, sfn);
    }

    // The in-memory entry is current: sizes aren't written back on every write
    struct fat_file entry = fi->entry;
    if ((err = fat_dir_add(nd, new_d->d_name.data, &entry, &new_idx)))
        goto out;
    fat_dir_remove(&od, old_first, old_sfn);

    // add the new name before dropping the old one
    if (nd != &od && (err = fat_dir_flush(nd)))
        goto out;
    if ((err = fat_dir_flush(&od)))
        goto out;

    fi->entry = entry;
    fi->dir_clst = nd->first_clst;
    fi->dir_idx = new_idx;
    if (target)
        fat_drop_inode(target);
    if (S_ISDIR(inode->i_mode) && new_dir != old_dir && fat_set_dotdot(inode, new_dir))
        klog(LOG_WARN, "fat32_rename: failed to update '..' of %s\n", new_d->d_name.data);

out:
    if (nd != &od)
        fat_dir_put(nd);
out_old:
    fat_dir_put(&od);
    return err;
}
