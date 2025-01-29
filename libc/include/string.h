#ifndef _STRING_H
#define _STRING_H 1

#include <sys/cdefs.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *memcpy(void *restrict dst, const void *restrict src, size_t sz);
void *memmove(void *dst, const void *src, size_t sz);
void *memset(void *buf, int val, size_t sz);
int memcmp(const void *a, const void *b, size_t sz);

char *strcpy(char *restrict dst, const char *restrict src);
char *strncpy(char *restrict dst, const char *restrict src, size_t n);
char *strcat(char *restrict dst, const char *restrict src);
char *strncat(char *restrict dst, const char *restrict src, size_t n);
int strcmp(const char *__s1, const char *__s2);
int strncmp(const char *__s1, const char *__s2, size_t __n);

size_t strlen(const char *str);
size_t strnlen(const char *str, size_t max);

char *strstr(const char *haystack, const char *needle);

char *strchr(const char *str, int c);
char *strrchr(const char *str, int c);

char *strtok(char *s, const char *delim);

char *strdup(const char *str);

#ifdef __cplusplus
}
#endif

#endif
