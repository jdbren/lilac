#ifndef _LILAC_LIBC_H
#define _LILAC_LIBC_H

#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>

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

int printf(const char *restrict format, ...);
int sprintf(char *restrict str, const char *restrict format, ...);
int vprintf(const char *restrict format, va_list args);

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

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

#endif
