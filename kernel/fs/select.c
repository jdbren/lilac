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

struct kpoll_cmd_struct {
    struct poll_request pollreq;
    struct poll_table_page *table;
    struct pollfd *fds;
    unsigned int nfds;
    struct task* current_task;
    int error;
    bool triggered;
};

static void free_poll_entry(struct poll_table_entry *entry)
{
    wait_ev_dequeue(entry->wait_address, &entry->wait);
    fput(entry->filp);
}

static void poll_freewait(struct kpoll_cmd_struct *kpoll_cmd)
{
    struct poll_table_page *p = kpoll_cmd->table;

    while (p) {
        struct poll_table_entry *entry;
        struct poll_table_page *old;

        for (entry = p->entries; entry < p->entry; entry++)
            free_poll_entry(entry);
        old = p;
        p = p->next;
        kfree(old);
    }
    kpoll_cmd->table = NULL;
}

#define POLL_TABLE_FULL(table) \
    ((unsigned long)((table)->entry+1) > PAGE_SIZE + (unsigned long)(table))

static struct poll_table_entry *poll_get_entry(struct kpoll_cmd_struct *p)
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

static int __pollwake(struct wq_entry *wait)
{
    struct kpoll_cmd_struct *kpoll_cmd = wait->private;

    /*
     * Although this function is called under waitqueue lock, LOCK
     * doesn't imply write barrier and the users expect write
     * barrier semantics on wakeup functions.  The following
     * smp_wmb() is equivalent to smp_wmb() in try_to_wake_up()
     * and is paired with smp_store_mb() in poll_schedule_timeout.
     */
    // smp_wmb();
    __asm__ ("" ::: "memory");
    WRITE_ONCE(kpoll_cmd->triggered, 1);

    set_task_running(kpoll_cmd->current_task);
    return 0;
}

static int pollwake(struct wq_entry *wait, int key) {
    struct poll_table_entry *entry;

    entry = container_of(wait, struct poll_table_entry, wait);
    if (key && !((poll_mask_t)(key) & entry->key))
        return 0;
    return __pollwake(wait);
}

static void __pollwait(struct file *filp, struct waitqueue *wait_address,
                struct poll_request *p)
{
    struct kpoll_cmd_struct *kpfd_list = container_of(p, struct kpoll_cmd_struct, pollreq);
    struct poll_table_entry *entry = poll_get_entry(kpfd_list);
    if (!entry)
        return;
    entry->filp = get_file(filp);
    entry->wait_address = wait_address;
    entry->key = p->mask;
    entry->wait.wakeup = pollwake;
    entry->wait.private = kpfd_list;
    entry->wait.task = current;
    INIT_LIST_HEAD(&entry->wait.entry);
    wait_ev_queue(wait_address, &entry->wait);
}

static int
poll_schedule_timeout(struct kpoll_cmd_struct *pwq, int state, ktime_t *expires)
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

static int do_poll(struct kpoll_cmd_struct *kpoll_cmd, ktime_t *expires)
{
    struct poll_request *pr = &kpoll_cmd->pollreq;
    struct pollfd *pfd;
    int count = 0;
    bool timed_out = false;

    if (expires && *expires == 0) {
        pr->qproc = NULL;
        timed_out = 1;
    }

    for (;;) {
        // loop through kpoll_cmd->fds and check events
        for (unsigned int i = 0; i < kpoll_cmd->nfds; i++) {
            pfd = &kpoll_cmd->fds[i];
            poll_mask_t mask = do_pollfd(pfd, pr);
            pfd->revents = mask;
            if (mask) {
                count++;
                pr->qproc = NULL;
            }
        }
        pr->qproc = NULL;
        if (!count) {
            count = kpoll_cmd->error;
            if (sigispending(current))
                count = -ERESTARTNOHAND;
        }
        if (count || timed_out)
            break;

        if (!poll_schedule_timeout(kpoll_cmd, TASK_SLEEPING, expires))
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

SYSCALL_DECL6(pselect, int, nfds, fd_set __user *, readfds, fd_set __user *,
    writefds, fd_set __user *, exceptfds, const struct timespec __user *, timeout,
    const struct sigset_argpack __user *, sig)
{
    return -ENOSYS;
}

int do_poll_syscall(struct pollfd __user *fds, unsigned int nfds, ktime_t *expires)
{
    struct kpoll_cmd_struct kpoll_cmd = {
        .fds = NULL,
        .nfds = nfds,
        .error = 0,
        .pollreq = { .qproc = __pollwait, .mask = 0 },
        .current_task = current,
    };
    int ret;

    if (nfds > FD_MAX)
        return -EINVAL;

    if (nfds) {
        kpoll_cmd.fds = kcalloc(nfds, sizeof(struct pollfd));
        if (!kpoll_cmd.fds)
            return -ENOMEM;

        if (copy_from_user(kpoll_cmd.fds, fds, nfds * sizeof(struct pollfd))) {
            kfree(kpoll_cmd.fds);
            return -EFAULT;
        }
    }

    ret = do_poll(&kpoll_cmd, expires);
    poll_freewait(&kpoll_cmd);

    if (nfds && copy_to_user(fds, kpoll_cmd.fds, nfds * sizeof(struct pollfd))) {
        ret = -EFAULT;
    }

    kfree(kpoll_cmd.fds);
    return ret;
}

SYSCALL_DECL3(poll, struct pollfd __user *, fds, unsigned int, nfds, int, timeout)
{
    ktime_t to, *expires = &to;

    if (timeout == 0) {
        to = 0;
    } else if (timeout > 0) {
        to = ktime_add_ms(ktime_get(), timeout);
    } else {
        expires = NULL;
    }

    return do_poll_syscall(fds, nfds, expires);
}

SYSCALL_DECL5(ppoll, struct pollfd __user *, fds, unsigned int, nfds,
    const struct timespec __user *, tmo_p, const sigset_t __user *, sigmask,
    size_t, sigsetsize)
{
    struct timespec ts;
    ktime_t ktimeout, *expires = &ktimeout;
    sigset_t ksigmask, oldmask;

    if (sigsetsize != sizeof(sigset_t))
        return -EINVAL;

    if (tmo_p) {
        if (copy_from_user(&ts, tmo_p, sizeof(struct timespec)))
            return -EFAULT;
        ktimeout = timespec_to_ktime(ts);
        if (ktimeout < 0)
            return -EINVAL;
        if (ktimeout != 0)
            ktimeout = ktime_add(ktime_get(), ktimeout);
    } else {
        expires = NULL;
    }

    if (sigmask) {
        if (copy_from_user(&ksigmask, sigmask, sigsetsize))
            return -EFAULT;
        oldmask = current->blocked;
        current->blocked = proc_sigmask(ksigmask);
    }

    long ret = do_poll_syscall(fds, nfds, expires);

    if (sigmask) {
        current->saved_sigmask = oldmask;
        current->flags.restore_sigmask = 1;
    }

    return ret;
}
