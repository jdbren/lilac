#ifndef _LILAC_SYSCALL_H
#define _LILAC_SYSCALL_H

#include <lilac/types.h>

#ifdef ARCH_x86
#define syscall __attribute__((regparm(0)))
#else
#define syscall
#endif


syscall long sys_restart(void);
syscall long sys_exit(int status);
syscall long sys_fork(void);
syscall long sys_read(int fd, void *buf, size_t count);
syscall long sys_write(int fd, const void *buf, size_t count);
syscall long sys_open(const char *path, int flags, int mode);
syscall long sys_close(int fd);
syscall long sys_getdents(unsigned int fd, struct dirent *dirp, size_t count);


#define __SC_DECL(t, a)	t a
#define __SC_TYPE(t, a)	t
#define __SC_ARGS(t, a)	a

#define __MAP0(m,...)
#define __MAP1(m,t,a,...) m(t,a)
#define __MAP2(m,t,a,...) m(t,a), __MAP1(m,__VA_ARGS__)
#define __MAP3(m,t,a,...) m(t,a), __MAP2(m,__VA_ARGS__)
#define __MAP4(m,t,a,...) m(t,a), __MAP3(m,__VA_ARGS__)
#define __MAP5(m,t,a,...) m(t,a), __MAP4(m,__VA_ARGS__)
#define __MAP6(m,t,a,...) m(t,a), __MAP5(m,__VA_ARGS__)
#define __MAP(n,...) __MAP##n(__VA_ARGS__)

#define SYSCALL_DECLn(n, name, ...) \
syscall long sys_##name(__MAP(n, __SC_DECL, __VA_ARGS__)) \
{ \
    return name(__MAP(n, __SC_ARGS, __VA_ARGS__)); \
}

#define SYSCALL_DECL0(name, ...) SYSCALL_DECLn(0, name, __VA_ARGS__)
#define SYSCALL_DECL1(name, ...) SYSCALL_DECLn(1, name, __VA_ARGS__)
#define SYSCALL_DECL2(name, ...) SYSCALL_DECLn(2, name, __VA_ARGS__)
#define SYSCALL_DECL3(name, ...) SYSCALL_DECLn(3, name, __VA_ARGS__)
#define SYSCALL_DECL4(name, ...) SYSCALL_DECLn(4, name, __VA_ARGS__)
#define SYSCALL_DECL5(name, ...) SYSCALL_DECLn(5, name, __VA_ARGS__)

#endif
