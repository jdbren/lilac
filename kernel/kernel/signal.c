#include <lilac/signal.h>

#include <lilac/lilac.h>
#include <lilac/syscall.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/uaccess.h>
#include <lilac/wait.h>

#define TERM_SIG (_SIGHUP | _SIGINT | _SIGKILL | _SIGPIPE | _SIGALRM | _SIGTERM)
#define CORE_SIG (_SIGQUIT | _SIGILL | _SIGABRT | _SIGSEGV | _SIGFPE)
#define STOP_SIG (_SIGSTOP | _SIGTSTP | _SIGTTIN | _SIGTTOU)

int handle_signal(void)
{
    int sig;
    sigset_t pending = current->pending.signal & ~current->blocked;

    if (pending == 0)
        return 0; // No pending unblocked signals
    else if (pending & _SIGKILL)
        sig = SIGKILL;
    else if (pending & _SIGSTOP)
        sig = SIGSTOP;
    else
        sig = __builtin_ffs(pending) - 1;

    struct ksigaction *ka = &current->sighandlers->actions[sig];
    int sig_bit = (1 << sig);
    current->pending.signal &= ~sig_bit;
    if (current->pending.signal == 0)
        current->flags.sig_pending = 0;

    if (ka->sa.sa_handler == SIG_IGN) {
        klog(LOG_WARN, "handle_signal: Signal %d ignored by process %d\n", sig, current->pid);
    } else if (ka->sa.sa_handler == SIG_DFL) {
        if (sig_bit & (TERM_SIG | CORE_SIG)) { // core dump not implemented
            klog(LOG_INFO, "Process %d received termination signal %d, exiting\n", current->pid, sig);
            current->exit_status = WSIGNALED(sig);
            do_exit();
        } else if (sig_bit & STOP_SIG) {
            klog(LOG_WARN, "signal: stop signal %d not implemented", sig);
        } else if (sig_bit & _SIGCHLD) {
            return 0; // Ignore SIGCHLD by default
        } else {
            klog(LOG_WARN, "Unhandled signal %d in process %d\n", sig, current->pid);
        }
    } else {
        klog(LOG_DEBUG, "Delivering signal %d to process %d\n", sig, current->pid);
        arch_prepare_signal(ka->sa.sa_handler, sig);
        current->blocked |= ka->sa.sa_mask;
        if (!(ka->sa.sa_flags & SA_NODEFER))
            current->blocked |= sig_bit;
        current->flags.signaled = 1;
    }

    return 0;
}

SYSCALL_DECL0(sigreturn)
{
    klog(LOG_DEBUG, "sigreturn called by process %d\n", current->pid);
    arch_restore_post_signal();
    current->flags.signaled = 1;
    return 0;
}

int do_raise(struct task *p, int sig)
{
    if (sig <= 0 || sig >= _NSIG) {
        klog(LOG_ERROR, "Invalid signal %d\n", sig);
        return -EINVAL;
    }
#ifdef DEBUG_SIGNAL
    klog(LOG_DEBUG, "Raising signal %d for process %d\n", sig, p->pid);
#endif
    sigset_t pending = p->pending.signal;
    struct ksigaction *ka = &p->sighandlers->actions[sig];

    if (sig & pending) {
        klog(LOG_DEBUG, "Signal %d already pending for process %d\n", sig, p->pid);
        return 0; // Signal already pending
    }

    if (ka->sa.sa_handler == SIG_IGN) {
        klog(LOG_DEBUG, "Signal %d ignored by process %d\n", sig, p->pid);
        return PTR_ERR(SIG_IGN); // Ignored signal
    }

    p->pending.signal |= (1 << sig);
    p->flags.sig_pending = 1;

    if (p->state == TASK_SLEEPING && !signal_blocked(p, sig)) {
        klog(LOG_DEBUG, "Waking up process %d for signal %d\n", p->pid, sig);
        p->flags.interrupted = 1;
        set_task_running(p);
    }

    return 0;
}

int kill_pgrp(int pgid, int sig)
{
    klog(LOG_DEBUG, "kill_pgrp called with pgid=%d, sig=%d\n", pgid, sig);
    if (sig < 0 || sig > _NSIG)
        return -EINVAL;

    if (pgid < 0)
        return -ESRCH;

    struct task *p;
    bool found = false;
    pgrp_for_each(p, pgid) {
        found = true;
        klog(LOG_DEBUG, "Sending signal %d to process %d in group %d\n", sig, p->pid, pgid);
        do_raise(p, sig);
    }

    if (!found) {
        klog(LOG_DEBUG, "kill_pgrp: No such process group %d\n", pgid);
        return -ESRCH;
    }

    return 0;
}

SYSCALL_DECL2(kill, int, pid, int, sig)
{
    klog(LOG_DEBUG, "kill called with pid=%d, sig=%d\n", pid, sig);
    if (sig < 0 || sig > _NSIG)
        return -EINVAL;

    if (pid == 0) {
        return kill_pgrp(current->pgid, sig);
    } else if (pid == -1) {
        return -EOPNOTSUPP;
    } else if (pid < -1) {
        return kill_pgrp(-pid, sig);
    }

    struct task *p = find_child_by_pid(current, pid);
    if (!p) {
        klog(LOG_DEBUG, "kill: No such child process %d\n", pid);
        return -ESRCH;
    }

    if (sig == 0) {
        klog(LOG_DEBUG, "kill: No-op signal 0 to process %d\n", pid);
        return 0; // No-op signal
    }

    klog(LOG_DEBUG, "Sending signal %d to process %d\n", sig, pid);
    return do_raise(p, sig);
}


SYSCALL_DECL2(signal, int, sig, __sighandler_t, handler)
{
    if (sig < 0 || sig >= _NSIG)
        return -EINVAL;

    if (sig == SIGKILL || sig == SIGSTOP) {
        klog(LOG_WARN, "signal: Cannot change handler for signal %d\n", sig);
        return -EINVAL;
    }

    struct ksigaction *ka = &current->sighandlers->actions[sig];
    __sighandler_t old_handler = ka->sa.sa_handler;
    ka->sa.sa_handler = handler;

    klog(LOG_DEBUG, "Signal %d handler set to %p\n", sig, handler);
    return (long)old_handler;
}

SYSCALL_DECL3(sigaction, int, signum, const struct sigaction *, act, struct sigaction *, oldact)
{
    if (signum <= 0 || signum >= _NSIG || signum == SIGKILL || signum == SIGSTOP)
        return -EINVAL;

    if (act && !access_ok(act, sizeof(struct sigaction))) {
        klog(LOG_WARN, "sigaction: Invalid user memory for new action\n");
        return -EFAULT;
    }
    if (oldact && !access_ok(oldact, sizeof(struct sigaction))) {
        klog(LOG_WARN, "sigaction: Invalid user memory for old action\n");
        return -EFAULT;
    }

    struct ksigaction *ka = &current->sighandlers->actions[signum];

    if (oldact) {
        oldact->sa_handler = ka->sa.sa_handler;
        oldact->sa_flags = ka->sa.sa_flags;
        oldact->sa_mask = ka->sa.sa_mask;
    }

    if (act) {
        ka->sa.sa_handler = act->sa_handler;
        ka->sa.sa_flags = act->sa_flags;
        ka->sa.sa_mask = act->sa_mask;
    }
#ifdef DEBUG_SIGNAL
    klog(LOG_DEBUG, "sigaction: Signal %d action updated to %p\n", signum, ka->sa.sa_handler);
#endif
    return 0;
}

SYSCALL_DECL3(sigprocmask, int, how, const sigset_t *, set, sigset_t *, oldset)
{
    if (set && !access_ok(set, sizeof(sigset_t))) {
        klog(LOG_WARN, "sigprocmask: Invalid user memory for new mask\n");
        return -EFAULT;
    }
    if (oldset && !access_ok(oldset, sizeof(sigset_t))) {
        klog(LOG_WARN, "sigprocmask: Invalid user memory for old mask\n");
        return -EFAULT;
    }

    if (oldset) {
        *oldset = current->blocked;
    }

    if (set) {
        sigset_t new_mask = *set;
        switch (how) {
            case SIG_BLOCK:
                current->blocked |= new_mask;
                break;
            case SIG_UNBLOCK:
                current->blocked &= ~new_mask;
                break;
            case SIG_SETMASK:
                current->blocked = new_mask;
                break;
            default:
                return -EINVAL;
        }
        klog(LOG_DEBUG, "sigprocmask: Updated signal mask to 0x%lx\n", current->blocked);
    }

    return 0;
}

SYSCALL_DECL1(sigpending, sigset_t *, set)
{
    if (!access_ok(set, sizeof(sigset_t))) {
        klog(LOG_WARN, "sigpending: Invalid user memory for pending signals\n");
        return -EFAULT;
    }

    *set = current->pending.signal;
    klog(LOG_DEBUG, "sigpending: Pending signals for process %d: 0x%lx\n", current->pid, *set);
    return 0;
}

SYSCALL_DECL1(sigsuspend, sigset_t*, set)
{
    if (!access_ok(set, sizeof(sigset_t))) {
        klog(LOG_WARN, "sigsuspend: Invalid user memory for pending signals\n");
        return -EFAULT;
    }

    sigset_t oldmask = current->blocked;
    klog(LOG_DEBUG, "sigsuspend: changing mask from %lx to %lx", oldmask, *set);
    current->blocked = *set;

    while (!current->flags.sig_pending) {
        current->state = TASK_SLEEPING;
        yield();
    }

    current->blocked = oldmask;
    return -EINTR;
}
