#include <fs/select.h>
#include <lilac/fs.h>
#include <lilac/sched.h>
#include <lilac/signal.h>
#include <lilac/syscall.h>
#include <lilac/time.h>
#include <lilac/timer.h>
#include <lilac/uaccess.h>
#include <mm/kmalloc.h>
#include <user/poll.h>

struct poll_table_entry {
    struct file *filp;
    poll_mask_t key;
    struct wq_entry wait;
    struct waitqueue *wait_address;
};

struct poll_table_page {
    struct poll_table_page * next;
    struct poll_table_entry * entry;
    struct poll_table_entry entries[];
};

struct kpollfd_list {
    struct poll_request pollreq;
    struct poll_table_page *table;
    struct pollfd *fds;
    unsigned int nfds;
    struct task* current_task;
    int error;
    bool triggered;
};

#define POLL_TABLE_FULL(table) \
    ((unsigned long)((table)->entry+1) > PAGE_SIZE + (unsigned long)(table))

static struct poll_table_entry *poll_get_entry(struct kpollfd_list *p)
{
    struct poll_table_page *table = p->table;

    if (!table || POLL_TABLE_FULL(table)) {
        struct poll_table_page *new_table;

        new_table = kmalloc(PAGE_SIZE);
        if (!new_table) {
            p->error = -ENOMEM;
            return NULL;
        }
        new_table->entry = new_table->entries;
        new_table->next = table;
        p->table = new_table;
        table = new_table;
    }

    return table->entry++;
}

static int pollwake(struct wq_entry *wait) {
    return 0;
}

static void __pollwait(struct file *filp, struct waitqueue *wait_address,
                struct poll_request *p)
{
    struct kpollfd_list *kpfd_list = container_of(p, struct kpollfd_list, pollreq);
    struct poll_table_entry *entry = poll_get_entry(kpfd_list);
    if (!entry)
        return;
    entry->filp = get_file(filp);
    entry->wait_address = wait_address;
    entry->key = p->mask;
    entry->wait.wakeup = pollwake;
    entry->wait.private = kpfd_list;
    wait_ev_queue(wait_address, &entry->wait);
}

static int
poll_schedule_timeout(struct kpollfd_list *pwq, int state, ktime_t *expires)
{
    int rc = -EINTR;

    set_current_state(state);
    if (!READ_ONCE(pwq->triggered))
        rc = sleep_until_timeout(expires);
    __set_current_state(TASK_RUNNING);

    /*
     * Prepare for the next iteration.
     *
     * The following smp_store_mb() serves two purposes.  First, it's
     * the counterpart rmb of the wmb in pollwake() such that data
     * written before wake up is always visible after wake up.
     * Second, the full barrier guarantees that triggered clearing
     * doesn't pass event check of the next iteration.  Note that
     * this problem doesn't exist for the first iteration as
     * add_wait_queue() has full barrier semantics.
     */
    WRITE_ONCE(pwq->triggered, 0);
    __asm__ ("mfence" ::: "memory");

    return rc;
}


/*
 * Fish for pollable events on the pollfd->fd file descriptor. We're only
 * interested in events matching the pollfd->events mask, and the result
 * matching that mask is both recorded in pollfd->revents and returned.
 */
static inline poll_mask_t
do_pollfd(struct pollfd *pollfd, struct poll_request *pollreq)
{
    int fd = pollfd->fd;
    poll_mask_t mask, filter;

    if (unlikely(fd < 0))
        return 0;

    struct file *f = get_file_handle(fd);
    if (IS_ERR_OR_NULL(f))
        return POLLNVAL;

    filter = pollfd->events | POLLERR | POLLHUP;
    pollreq->mask = filter;
    mask = vfs_poll(f, pollreq);

    return mask & filter;
}

static int do_poll(struct kpollfd_list *poll_list, ktime_t *expires)
{
    struct poll_request *pr = &poll_list->pollreq;
    struct file *filep;
    struct pollfd *pfd;
    int count = 0;
    bool timed_out = false;

    if (expires && *expires == 0) {
        pr->qproc = NULL;
        timed_out = 1;
    }

    for (;;) {
        // loop through poll_list->fds and check events
        for (unsigned int i = 0; i < poll_list->nfds; i++) {
            pfd = &poll_list->fds[i];
            filep = get_file_handle(pfd->fd);
            if (IS_ERR(filep))
                continue;

            poll_mask_t mask = do_pollfd(pfd, pr);
            pfd->revents = mask;
            if (mask) {
                count++;
            }
        }

        if (!count) {
            count = poll_list->error;
            if (sigispending(current))
                count = -ERESTARTNOHAND;
        }
        if (count || timed_out)
            break;

        if (!poll_schedule_timeout(poll_list, TASK_SLEEPING, expires))
            timed_out = 1;
    }

    return count;
}

SYSCALL_DECL5(select, int, nfds, fd_set __user *, readfds, fd_set __user *,
    writefds, fd_set __user *, exceptfds, struct timeval __user *, timeout)
{
    return -ENOSYS;
}

struct sigset_argpack {
    const sigset_t __user *p;
    size_t size;
};

SYSCALL_DECL6(pselect6, int, nfds, fd_set __user *, readfds, fd_set __user *,
    writefds, fd_set __user *, exceptfds, const struct timespec __user *, timeout,
    const struct sigset_argpack __user *, sig)
{
    return -ENOSYS;
}

SYSCALL_DECL3(poll, struct pollfd __user *, fds, unsigned int, nfds, int, timeout)
{
    struct kpollfd_list poll_list = {
        .fds = NULL,
        .nfds = nfds,
        .error = 0,
        .pollreq = { .qproc = __pollwait, .mask = 0 },
    };
    ktime_t to, *expires = &to;
    int ret;

    if (nfds == 0)
        return 0;

    if (timeout == 0) {
        to = 0;
    } else if (timeout > 0) {
        to = ktime_add_ms(ktime_get(), timeout);
    } else {
        expires = NULL;
    }

    poll_list.fds = kcalloc(nfds, sizeof(struct pollfd));
    if (!poll_list.fds)
        return -ENOMEM;

    if (copy_from_user(poll_list.fds, fds, nfds * sizeof(struct pollfd))) {
        kfree(poll_list.fds);
        return -EFAULT;
    }

    ret = do_poll(&poll_list, expires);

    kfree(poll_list.fds);
    return ret;
}

SYSCALL_DECL5(ppoll, struct pollfd __user *, fds, unsigned int, nfds,
    const struct timespec __user *, to, const sigset_t __user *, sigmask,
    size_t, sigsetsize)
{
    return -ENOSYS;
}
