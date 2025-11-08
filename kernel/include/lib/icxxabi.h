#ifndef _ICXXABI_H
#define _ICXXABI_H

#define ATEXIT_MAX_FUNCS	128

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned uarch_t;

int __cxa_atexit(void (*f)(void *), void *objptr, void *dso);
void __cxa_finalize(void *f);

void init_ctors(void);
void cpptest();

#ifdef __cplusplus
};
#endif

#endif
