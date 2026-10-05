#include <lilac/resource.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/syscall.h>
#include <lilac/uaccess.h>
#include <lilac/errno.h>
#include <lilac/fdtable.h>
#include <lilac/config.h>
#include <lilac/signal.h>
#include <lilac/timer.h>
#include <mm/page.h>

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

void copy_rlimits(struct rlimit *dst, struct task *leader)
{
    memcpy(dst, leader->rlim, sizeof(leader->rlim));
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

struct sysinfo {
    long uptime;
    unsigned long loads[3];
    unsigned long totalram;
    unsigned long freeram;
    unsigned long sharedram;
    unsigned long bufferram;
    unsigned long totalswap;
    unsigned long freeswap;
    unsigned short procs;
    unsigned short pad;
    unsigned long totalhigh;
    unsigned long freehigh;
    unsigned int mem_unit;
};

SYSCALL_DECL1(sysinfo, struct sysinfo*, info)
{
    struct sysinfo si = {0};
    unsigned long free = num_free_frames();

    si.uptime = ktime_get() / 1000000000ULL;
    si.freeram = free;
    si.totalram = free + num_used_frames();
    si.mem_unit = PAGE_SIZE;
    if (copy_to_user(info, &si, sizeof(si)))
        return -EFAULT;
    return 0;
}
