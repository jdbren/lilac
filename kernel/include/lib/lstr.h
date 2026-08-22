#ifndef LILAC_STR_H
#define LILAC_STR_H

struct lstr {
    char *data;
    unsigned long len;
};

struct lstr lstr_from_cstr(const char *cstr);
struct lstr lstr_from_cstr_len(const char *cstr, unsigned long len);

struct lstr * lstr_alloc(const char *cstr);
struct lstr * lstr_alloc_len(const char *cstr, unsigned long len);
void lstr_free(struct lstr *s);

int lstrcmp(const struct lstr *s1, const struct lstr *s2);
int lstrncmp(const struct lstr *s1, const struct lstr *s2, unsigned long n);
int lstrcmp_cstr(const struct lstr *s1, const char *cstr);
int lstrncmp_cstr(const struct lstr *s1, const char *cstr, unsigned long n);

#endif
