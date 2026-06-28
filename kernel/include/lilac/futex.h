#ifndef LILAC_FUTEX_H
#define LILAC_FUTEX_H

#include <lilac/types.h>
#include <lilac/uaccess.h>

void futex_init(void);

int futex_wait(int __user *uaddr, int val, ktime_t abs_to);
int futex_wake(int __user *uaddr, int count);

#endif
