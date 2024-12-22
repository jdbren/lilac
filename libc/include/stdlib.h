#ifndef _STDLIB_H
#define _STDLIB_H 1

#include <sys/cdefs.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0

__attribute__((__noreturn__)) void abort(void);
__attribute__((__noreturn__)) void exit(int status);
__attribute__((__noreturn__)) void _Exit(int status);

#ifdef __cplusplus
}
#endif

#endif
