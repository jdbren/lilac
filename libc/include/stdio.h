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
int putchar(int c);
int puts(const char *str);

#ifdef __cplusplus
}
#endif

#endif
