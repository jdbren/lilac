#ifndef _STDIO_H
#define _STDIO_H 1

#include <sys/cdefs.h>
#include <stdarg.h>

#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif

int printf(const char *restrict format, ...);
int vprintf(const char *restrict format, va_list args);
// int fprintf(FILE *restrict stream, const char *restrict format, ...);
// int vfprintf(FILE *restrict stream, const char *restrict format, va_list args);
int putchar(int c);
int puts(const char *str);

int scanf(const char *restrict format, ...);
int vscanf(const char *restrict format, va_list args);
// int fscanf(FILE *restrict stream, const char *restrict format, ...);
// int vfscanf(FILE *restrict stream, const char *restrict format, va_list args);
int sscanf(const char *restrict buffer, const char *restrict format, ...);
int vsscanf(const char *restrict buffer, const char *restrict format, va_list args);


#ifdef __cplusplus
}
#endif

#endif
