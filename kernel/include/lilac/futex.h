#ifndef LILAC_FUTEX_H
#define LILAC_FUTEX_H

void futex_init(void);

int futex_wait(int *uaddr, int val);
int futex_wake(int *uaddr, int count);

#endif
