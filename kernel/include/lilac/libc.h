#ifndef _LILAC_LIBC_H
#define _LILAC_LIBC_H

#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0

int atoi(const char*);

int abs(int);

int toupper(int ch);
int tolower(int ch);

int isprint(int c);
int isspace(int c);
int isdigit(int arg);
char isxdigit(char x);

int log2(unsigned int x);

#define EOF (-1)

int putchar(int c);
int puts(const char *str);

int printf(const char *__restrict__ format, ...);
int vprintf(const char *__restrict__ format, va_list args);
int sprintf(char *__restrict__ str, const char *__restrict__ fmt, ...);
int snprintf(char *__restrict__ str, size_t size, const char *__restrict__ format, ...);
int vsnprintf(char *__restrict__ buf, size_t size, const char *__restrict__ fmt, va_list args);

void *memcpy(void *__restrict__ dst, const void *__restrict__ src, size_t sz);
void *memmove(void *dst, const void *src, size_t sz);
void *memset(void *buf, int val, size_t sz);
int memcmp(const void *a, const void *b, size_t sz);

char *strcpy(char *__restrict__ dst, const char *__restrict__ src);
char *strncpy(char *__restrict__ dst, const char *__restrict__ src, size_t n);
char *strcat(char *__restrict__ dst, const char *__restrict__ src);
char *strncat(char *__restrict__ dst, const char *__restrict__ src, size_t n);
int strcmp(const char *__s1, const char *__s2);
int strncmp(const char *__s1, const char *__s2, size_t __n);

size_t strlen(const char *str);
size_t strnlen(const char *str, size_t max);

char *strstr(const char *haystack, const char *needle);

char *strchr(const char *str, int c);
char *strrchr(const char *str, int c);

char *strtok(char *s, const char *delim);

char *strdup(const char *str);
char *strndup(const char *str, size_t n);

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#ifdef __cplusplus
}
#endif

#endif
