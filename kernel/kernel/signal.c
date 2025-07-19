#include <lilac/lilac.h>
#include <lilac/syscall.h>
#include <lilac/signal.h>
#include <lilac/process.h>
#include <lilac/wait.h>

int do_raise(struct task *task, int sig)
{
    switch (sig) {
    case SIGKILL:
        task->exit_status = WSIGNALED(sig);
        task->pending.signal |= (1 << sig);
        break;
    default:
        klog(LOG_DEBUG, "do_raise: unimplemented signal %d\n", sig);
    }
    return 0;
}

SYSCALL_DECL2(kill, int, pid, int, sig)
{
    if (sig < 0 || sig > _NSIG)
        return -EINVAL;

    if (pid < 0)
        return -ESRCH;

    struct task *p = find_child_by_pid(current, pid);
    if (!p) {
        klog(LOG_DEBUG, "kill: No such child process %d\n", pid);
        return -ESRCH;
    }

    klog(LOG_DEBUG, "Sending signal %d to process %d\n", sig, pid);
    return do_raise(p, sig);
}
