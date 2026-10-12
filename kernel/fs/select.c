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
    struct task *current_task;
    unsigned int nfds;
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

//
// Polling logic
//

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

//
// Timeout and restart helpers
//

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
 * Convert relative to absolute deadline
 */
static int poll_select_set_timeout(ktime_t *to, const struct timespec *ts)
{
    if (!timespec_valid_strict(ts))
        return -EINVAL;
    *to = timespec_to_ktime(*ts);
    if (*to != 0)
        *to = ktime_add_safe(ktime_get(), *to);
    return 0;
}

/*
 * When select is interrupted and will be restarted, store the
 * time remaining back into the user's timeout to keep the restart accurate
 */
static int poll_select_finish(ktime_t *expires, void __user *p, bool timeval,
    int ret)
{
    if (ret != -ERESTARTNOHAND || !p || !expires || *expires == 0)
        return ret;

    ktime_t left = *expires - ktime_get();
    if (left < 0)
        left = 0;

    int err;
    if (timeval) {
        struct timeval tv = ns_to_timeval(left);
        err = copy_to_user(p, &tv, sizeof(tv));
    } else {
        struct timespec ts = { left / NS_PER_SEC, left % NS_PER_SEC };
        err = copy_to_user(p, &ts, sizeof(ts));
    }
    return err ? -EINTR : ret;
}

/*
 * Undo the temporary sigmask. When interrupted, defer the
 * restore so the signal is delivered under the temporary mask
 */
static void poll_select_restore_sigmask(sigset_t oldset, long ret)
{
    if (ret == -ERESTARTNOHAND || ret == -EINTR) {
        current->saved_sigmask = oldset;
        current->flags.restore_sigmask = 1;
    } else {
        current->blocked = oldset;
    }
}

static int do_select(struct kpoll_cmd_struct *kpoll_cmd, int nfds,
    struct fd_sets *in, struct fd_sets *out, ktime_t *expires)
{
    struct poll_request *pr = &kpoll_cmd->pollreq;
    int count = 0;
    bool timed_out = false;

    if (expires && *expires == 0) {
        pr->qproc = NULL;
        timed_out = 1;
    }

    for (;;) {
        for (int fd = 0; fd < nfds; fd++) {
            bool r = FD_ISSET(fd, &in->readfds);
            bool w = FD_ISSET(fd, &in->writefds);
            bool e = FD_ISSET(fd, &in->exceptfds);
            if (!(r || w || e))
                continue;

            struct file *f = get_file_inc_ref(fd);
            if (IS_ERR_OR_NULL(f)) {
                count = -EBADF;
                goto out;
            }

            pr->mask = (r ? POLLIN_SET : 0) | (w ? POLLOUT_SET : 0) | (e ? POLLEX_SET : 0);
            poll_mask_t m = vfs_poll(f, pr);
            fput(f);
            if (r && (m & POLLIN_SET))  { FD_SET(fd, &out->readfds);  count++; pr->qproc = NULL; }
            if (w && (m & POLLOUT_SET)) { FD_SET(fd, &out->writefds); count++; pr->qproc = NULL; }
            if (e && (m & POLLEX_SET))  { FD_SET(fd, &out->exceptfds); count++; pr->qproc = NULL; }
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

out:
    return count;
}

static int do_select_syscall(int nfds, fd_set __user *readfds, fd_set __user *writefds,
    fd_set __user *exceptfds, ktime_t *expires)
{
    struct kpoll_cmd_struct kpoll_cmd = {
        .error = 0,
        .pollreq = { .qproc = __pollwait, .mask = 0 },
        .current_task = current,
    };

    struct fd_sets *infds = NULL, *outfds = NULL;
    long ret = -EFAULT;

    if (nfds > FD_MAX || nfds < 0)
        return -EINVAL;

    infds = kzmalloc(sizeof(struct fd_sets));
    if (!infds)
        return -ENOMEM;
    outfds = kzmalloc(sizeof(struct fd_sets));
    if (!outfds) {
        ret = -ENOMEM;
        goto error;
    }

    if (readfds && get_user(infds->readfds, readfds))
        goto error;
    if (writefds && get_user(infds->writefds, writefds))
        goto error;
    if (exceptfds && get_user(infds->exceptfds, exceptfds))
        goto error;

    ret = do_select(&kpoll_cmd, nfds, infds, outfds, expires);
    poll_freewait(&kpoll_cmd);

    if (ret >= 0) {
        if (readfds && copy_to_user(readfds, &outfds->readfds, sizeof(fd_set)))
            ret = -EFAULT;
        if (writefds && copy_to_user(writefds, &outfds->writefds, sizeof(fd_set)))
            ret = -EFAULT;
        if (exceptfds && copy_to_user(exceptfds, &outfds->exceptfds, sizeof(fd_set)))
            ret = -EFAULT;
    }

error:
    kfree(infds);
    kfree(outfds);
    return ret;
}

SYSCALL_DECL5(select, int, nfds, fd_set __user *, readfds, fd_set __user *,
    writefds, fd_set __user *, exceptfds, struct timeval __user *, timeout)
{
    struct timeval tv;
    ktime_t to, *expires = NULL;

    if (timeout) {
        if (copy_from_user(&tv, timeout, sizeof(struct timeval)))
            return -EFAULT;
        struct timespec ts = timeval_to_timespec(tv);
        if (poll_select_set_timeout(&to, &ts))
            return -EINVAL;
        expires = &to;
    }

    int ret = do_select_syscall(nfds, readfds, writefds, exceptfds, expires);
    return poll_select_finish(expires, timeout, true, ret);
}

struct sigset_argpack {
    const sigset_t __user *p;
    size_t size;
};

SYSCALL_DECL6(pselect, int, nfds, fd_set __user *, readfds, fd_set __user *,
    writefds, fd_set __user *, exceptfds, const struct timespec __user *, timeout,
    const struct sigset_argpack __user *, sig)
{
    struct timespec ts;
    struct sigset_argpack sigargs;
    sigset_t oldset, newset;
    ktime_t to, *expires = NULL;

    if (timeout) {
        if (copy_from_user(&ts, timeout, sizeof(struct timespec)))
            return -EFAULT;
        if (poll_select_set_timeout(&to, &ts))
            return -EINVAL;
        expires = &to;
    }

    if (sig && copy_from_user(&sigargs, sig, sizeof(struct sigset_argpack)))
        return -EFAULT;

    if (sig) {
        oldset = current->blocked;
        if (sigargs.size != sizeof(sigset_t)) {
            klog(LOG_DEBUG, "Invalid sigset size: %lu\n", sigargs.size);
            return -EINVAL;
        }
        if (sigargs.p) {
            if (copy_from_user(&newset, sigargs.p, sizeof(sigset_t)))
                return -EFAULT;
            current->blocked = proc_sigmask(newset);
        }
    }

    int ret = do_select_syscall(nfds, readfds, writefds, exceptfds, expires);
    ret = poll_select_finish(expires, (void*)timeout, false, ret);

    if (sig)
        poll_select_restore_sigmask(oldset, ret);

    return ret;
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

    struct file *f = get_file_inc_ref(fd);
    if (IS_ERR_OR_NULL(f))
        return POLLNVAL;

    filter = pollfd->events | POLLERR | POLLHUP;
    pollreq->mask = filter;
    mask = vfs_poll(f, pollreq);
    fput(f);

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

static long do_restart_poll(struct restart_block *rb);

/**
 * Manages a poll syscall that can be restarted if interrupted
 * while keeping original timeout
 */
static int poll_restartable(struct pollfd __user *fds, unsigned int nfds, ktime_t *expires)
{
    int ret = do_poll_syscall(fds, nfds, expires);

    if (ret == -ERESTARTNOHAND) {
        current->restart_block = (struct restart_block){
            .fn = do_restart_poll,
            .deadline = expires ? *expires : 0,
            .poll = { .ufds = fds, .nfds = nfds, .has_timeout = expires != NULL },
        };
        ret = -ERESTART_RESTARTBLOCK;
    }
    return ret;
}

static long do_restart_poll(struct restart_block *rb)
{
    ktime_t to = rb->deadline;
    return poll_restartable(rb->poll.ufds, rb->poll.nfds,
        rb->poll.has_timeout ? &to : NULL);
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

    return poll_restartable(fds, nfds, expires);
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
        if (poll_select_set_timeout(&ktimeout, &ts))
            return -EINVAL;
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
    ret = poll_select_finish(tmo_p ? expires : NULL, (void*)tmo_p, false, ret);

    if (sigmask)
        poll_select_restore_sigmask(oldmask, ret);

    return ret;
}
