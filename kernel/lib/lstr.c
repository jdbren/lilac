#include <lib/lstr.h>
#include <lilac/libc.h>
#include <mm/kmalloc.h>

struct lstr lstr_from_cstr(const char *cstr)
{
    struct lstr s;
    s.len = strlen(cstr);
    s.data = (char *)cstr;
    return s;
}

struct lstr lstr_from_cstr_len(const char *cstr, unsigned long len)
{
    struct lstr s;
    s.len = len;
    s.data = (char *)cstr;
    return s;
}

struct lstr * lstr_alloc_len(const char *cstr, unsigned long len)
{
    struct lstr *s = kmalloc(sizeof(struct lstr));
    if (!s)
        return NULL;

    s->data = kmalloc(len + 1);
    if (!s->data) {
        kfree(s);
        return NULL;
    }

    memcpy(s->data, cstr, len);
    s->data[len] = '\0';
    s->len = len;

    return s;
}

struct lstr * lstr_alloc(const char *cstr)
{
    unsigned long len = 0;
    while (cstr[len] != '\0') {
        len++;
    }
    return lstr_alloc_len(cstr, len);
}

void lstr_free(struct lstr *s)
{
    if (s) {
        kfree(s->data);
        kfree(s);
    }
}

int lstrcmp(const struct lstr *s1, const struct lstr *s2)
{
    unsigned long min_len = (s1->len < s2->len) ? s1->len : s2->len;
    int cmp = memcmp(s1->data, s2->data, min_len);
    if (cmp != 0) {
        return cmp;
    }
    return (s1->len > s2->len) - (s1->len < s2->len);
}

int lstrncmp(const struct lstr *s1, const struct lstr *s2, unsigned long n)
{
    unsigned long min_len = (s1->len < s2->len) ? s1->len : s2->len;
    if (n < min_len) {
        min_len = n;
    }
    int cmp = memcmp(s1->data, s2->data, min_len);
    if (cmp != 0) {
        return cmp;
    }
    if (min_len < n) {
        return (s1->len > s2->len) - (s1->len < s2->len);
    }
    return 0;
}

int lstrcmp_cstr(const struct lstr *s1, const char *cstr)
{
    unsigned long cstr_len = 0;
    while (cstr[cstr_len] != '\0') {
        cstr_len++;
    }
    unsigned long min_len = (s1->len < cstr_len) ? s1->len : cstr_len;
    int cmp = memcmp(s1->data, cstr, min_len);
    if (cmp != 0) {
        return cmp;
    }
    return (s1->len > cstr_len) - (s1->len < cstr_len);
}

int lstrncmp_cstr(const struct lstr *s1, const char *cstr, unsigned long n)
{
    unsigned long cstr_len = 0;
    while (cstr[cstr_len] != '\0') {
        cstr_len++;
    }
    unsigned long min_len = (s1->len < cstr_len) ? s1->len : cstr_len;
    if (n < min_len) {
        min_len = n;
    }
    int cmp = memcmp(s1->data, cstr, min_len);
    if (cmp != 0) {
        return cmp;
    }
    if (min_len < n) {
        return (s1->len > cstr_len) - (s1->len < cstr_len);
    }
    return 0;
}
