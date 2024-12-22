#ifndef _STDIO_H
#define _STDIO_H 1

#include <sys/cdefs.h>
#include <stdarg.h>

#define EOF (-1)

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __iobuf {
    char *_ptr;
    int _cnt;
    char *_base;
    int _file;
} FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

#define stdin stdin
#define stdout stdout
#define stderr stderr

FILE *fopen(const char *restrict filename, const char *restrict mode);
int fclose(FILE *stream);
int fflush(FILE *stream);
int fread(void *ptr, int size, int nmemb, FILE *stream);
int fwrite(const void *ptr, int size, int nmemb, FILE *stream);

int fgetc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);
int getc(FILE *stream);
int getchar(void);
char *gets(char *s);

int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
int putc(int c, FILE *stream);
int putchar(int c);
int puts(const char *str);

int printf(const char *restrict format, ...);
int vprintf(const char *restrict format, va_list args);
int fprintf(FILE *restrict stream, const char *restrict format, ...);
int vfprintf(FILE *restrict stream, const char *restrict format, va_list args);

int scanf(const char *restrict format, ...);
int vscanf(const char *restrict format, va_list args);
int fscanf(FILE *restrict stream, const char *restrict format, ...);
int vfscanf(FILE *restrict stream, const char *restrict format, va_list args);
int sscanf(const char *restrict buffer, const char *restrict format, ...);
int vsscanf(const char *restrict buffer, const char *restrict format, va_list args);


#ifdef __cplusplus
}
#endif

#endif
