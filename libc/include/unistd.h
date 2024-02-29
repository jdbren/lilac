#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <sys/types.h>

int          close(int);
void         _exit(int);
int          execl(const char *, const char *, ...);
int          execv(const char *, char *const []);
pid_t        fork(void);
ssize_t      read(int, void *, size_t);
ssize_t      write(int, const void *, size_t);

#endif
