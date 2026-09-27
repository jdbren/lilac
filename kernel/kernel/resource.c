#include <lilac/resource.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/syscall.h>
#include <lilac/uaccess.h>
#include <lilac/errno.h>
#include <lilac/fdtable.h>
#include <lilac/config.h>
#include <lilac/signal.h>

/*
 * Defaults describe what this kernel actually allows. Infinite means the
 * kernel has no limit of its own; 0 means the feature doesn't exist.
 */
void init_rlimits(struct rlimit *rlim)
{
    for (int i = 0; i < RLIM_NLIMITS; i++)
        rlim[i] = (struct rlimit){ RLIM_INFINITY, RLIM_INFINITY };

    rlim[RLIMIT_NOFILE] = (struct rlimit){ FD_AUTO_MAX, FD_MAX };
    // Fixed-size stack mapping, it doesn't grow
    rlim[RLIMIT_STACK] = (struct rlimit){ __USER_STACK_SZ, __USER_STACK_SZ };
    // brk heap cap; mmap isn't counted
    rlim[RLIMIT_DATA] = (struct rlimit){ __USER_BRK_SZ, __USER_BRK_SZ };
    // Standard signals only: at most one pending per signal
    rlim[RLIMIT_SIGPENDING] = (struct rlimit){ _NSIG - 1, _NSIG - 1 };
    // Not implemented
    rlim[RLIMIT_CORE] = (struct rlimit){ 0, 0 };
    rlim[RLIMIT_MEMLOCK] = (struct rlimit){ 0, 0 };
    rlim[RLIMIT_MSGQUEUE] = (struct rlimit){ 0, 0 };
    rlim[RLIMIT_NICE] = (struct rlimit){ 0, 0 };
    rlim[RLIMIT_RTPRIO] = (struct rlimit){ 0, 0 };
}

static long do_prlimit(pid_t pid, unsigned int resource,
                       const struct rlimit *new, struct rlimit *old)
{
    struct rlimit knew;
    struct task *p;

    if (resource >= RLIM_NLIMITS)
        return -EINVAL;
    if (new) {
        if (copy_from_user(&knew, new, sizeof(knew)))
            return -EFAULT;
        if (knew.rlim_cur > knew.rlim_max)
            return -EINVAL;
    }

    p = pid ? get_task_by_pid(pid) : current;
    if (!p)
        return -ESRCH;
    struct rlimit *rlim = &p->tg_leader->rlim[resource];

    if (old && copy_to_user(old, rlim, sizeof(*rlim)))
        return -EFAULT;
    if (new)
        *rlim = knew;
    return 0;
}

SYSCALL_DECL2(getrlimit, unsigned int, resource, struct rlimit*, rlim)
{
    return do_prlimit(0, resource, NULL, rlim);
}

SYSCALL_DECL2(setrlimit, unsigned int, resource, const struct rlimit*, rlim)
{
    return do_prlimit(0, resource, rlim, NULL);
}

SYSCALL_DECL4(prlimit64, pid_t, pid, unsigned int, resource,
              const struct rlimit*, new, struct rlimit*, old)
{
    return do_prlimit(pid, resource, new, old);
}
