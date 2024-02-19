#ifndef _STRING_H
#define _STRING_H 1

#include <sys/cdefs.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int memcmp(const void *a, const void *b, size_t sz);
void* memcpy(void *restrict dst, const void *restrict src, size_t sz);
void* memmove(void *dst, const void *src, size_t sz);
void* memset(void *buf, int val, size_t sz);
size_t strlen(const char *str);

#ifdef __cplusplus
}
#endif

#endif
