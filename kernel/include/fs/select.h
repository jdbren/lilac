#ifndef LILAC_SELECT_H
#define LILAC_SELECT_H

#include <lilac/types.h>
#include <user/poll.h>
#include <asm/barrier.h>

#define FD_SETSIZE 1024

#define POLLIN_SET  (POLLIN | POLLRDNORM | POLLRDBAND | POLLHUP | POLLERR)
#define POLLOUT_SET (POLLOUT | POLLWRNORM | POLLWRBAND | POLLERR)
#define POLLEX_SET  (POLLPRI)

typedef unsigned long fd_mask;

typedef struct {
    unsigned long fds_bits[FD_SETSIZE / 8 / sizeof(long)];
} fd_set;

#define FD_ZERO(s) do { int __i; unsigned long *__b=(s)->fds_bits; \
    for(__i=sizeof (fd_set)/sizeof (long); __i; __i--) *__b++=0; } while(0)
#define FD_SET(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] |= (1UL<<((d)%(8*sizeof(long)))))
#define FD_CLR(d, s)   ((s)->fds_bits[(d)/(8*sizeof(long))] &= ~(1UL<<((d)%(8*sizeof(long)))))
#define FD_ISSET(d, s) !!((s)->fds_bits[(d)/(8*sizeof(long))] & (1UL<<((d)%(8*sizeof(long)))))

struct fd_sets {
    fd_set readfds;
    fd_set writefds;
    fd_set exceptfds;
};

struct poll_request;
struct waitqueue;
struct file;

typedef unsigned poll_mask_t;
typedef void (*poll_queue_proc)(struct file *, struct waitqueue *, struct poll_request *);

struct poll_request {
    poll_queue_proc qproc;
    poll_mask_t mask;
};

static inline void poll_wait(struct file * filp, struct waitqueue *wait_address,
    struct poll_request *p)
{
    if (p && p->qproc) {
        p->qproc(filp, wait_address, p);
        smp_mb();
    }
}

#endif // LILAC_SELECT_H
