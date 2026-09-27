#include <lilac/signal.h>

#include <lilac/lilac.h>
#include <lilac/syscall.h>
#include <lilac/process.h>
#include <lilac/sched.h>
#include <lilac/uaccess.h>
#include <lilac/wait.h>

#define TERM_SIG (_SIGHUP | _SIGINT | _SIGKILL | _SIGPIPE | _SIGALRM | _SIGTERM | _SIGUSR1 | \
    _SIGUSR2 | _SIGSTKFLT | _SIGVTALRM | _SIGPROF | _SIGIO | _SIGPWR)
#define CORE_SIG (_SIGQUIT | _SIGILL | _SIGABRT | _SIGSEGV | _SIGFPE | _SIGBUS | _SIGTRAP | \
    _SIGSYS | _SIGXCPU | _SIGXFSZ)
#define STOP_SIG (_SIGSTOP | _SIGTSTP | _SIGTTIN | _SIGTTOU)
#define DFL_IGN_SIG (_SIGCHLD | _SIGURG | _SIGWINCH | _SIGCONT)
static const sigset_t unblockable = _SIGKILL | _SIGSTOP;
static const sigset_t synchronous = _SIGSEGV | _SIGFPE | _SIGILL | _SIGBUS | _SIGTRAP;

int handle_signal(void)
{
    struct task *p = current;
    int sig;
    sigset_t pending = sig_get_active(p);

    if (pending == 0)
        return 0; // No pending unblocked signals
    else if (pending & _SIGKILL)
        sig = SIGKILL;
    else if (pending & _SIGSTOP)
        sig = SIGSTOP;
    else
        sig = __builtin_ffsl(pending);

    struct ksigaction *ka = &p->sighand->actions[sig];
    sigdelset(&p->pending, sig);

    sigset_t sig_bit = sigbit(sig);
    if (ka->sa.sa_handler == SIG_IGN) {
        klog(LOG_WARN, "handle_signal: Signal %d ignored by process %d\n", sig, p->pid);
    } else if (ka->sa.sa_handler == SIG_DFL) {
        if (sig_bit & (TERM_SIG | CORE_SIG)) { // core dump not implemented
            klog(LOG_INFO, "Process %d received termination signal %d, exiting\n", p->pid, sig);
            if (p->group_exit) {
                // killed as part of another thread's exit_group / fatal signal
                p->exit_status = p->group_exit_code;
            } else {
                p->exit_status = WSIGNALED(sig);
                zap_other_threads(p, p->exit_status);
            }
            do_exit();
        } else if (sig_bit & STOP_SIG) {
            klog(LOG_INFO, "handling stop signal %d\n", sig);
            set_task_stopped(p);
            p->exit_status = WSTOPPED(sig);
            do_raise(p->parent, SIGCHLD);
            p->flags.need_resched = 1;
        } else if (sig_bit & _SIGCONT) {
            klog(LOG_INFO, "Process %d received SIGCONT, continuing\n", p->pid);
        } else if (sig_bit & _SIGCHLD) {
            return 0; // Ignore SIGCHLD by default
        } else {
            klog(LOG_WARN, "Unhandled signal %d in process %d\n", sig, p->pid);
        }
    } else {
        klog(LOG_DEBUG, "Delivering signal %d to process %d\n", sig, p->pid);
        arch_restart_syscall(p, ka->sa.sa_flags & SA_RESTART);
        arch_prepare_signal(ka->sa.sa_handler, sig,
            (ka->sa.sa_flags & SA_SIGINFO) ? &p->siginfo[sig] : NULL,
            (ka->sa.sa_flags & SA_RESTORER) ? (void*)ka->sa.sa_restorer : NULL);
        if (ka->sa.sa_flags & SA_RESETHAND) {
            ka->sa.sa_handler = SIG_DFL;
            ka->sa.sa_flags &= ~SA_SIGINFO;
        }
        p->blocked |= ka->sa.sa_mask;
        if (!(ka->sa.sa_flags & SA_NODEFER))
            p->blocked |= sig_bit;
        p->flags.signaled = 1;
    }

    return 0;
}

SYSCALL_DECL0(pause)
{
    klog(LOG_DEBUG, "Process %d called pause\n", current->pid);
    set_task_sleeping(current);
    while (!sigispending(current))
        yield();
    return -EINTR;
}

SYSCALL_DECL0(sigreturn)
{
    klog(LOG_DEBUG, "sigreturn called by process %d\n", current->pid);
    current->flags.signaled = 1;
    return arch_restore_post_signal();
}

void do_kill(struct task *p, int sig)
{
    klog(LOG_INFO, "Process %d received fatal signal %d, terminating\n", p->pid, sig);
    p->exit_status = WCOREDUMP(sig);
    do_exit();
}

int do_raise(struct task *p, int sig)
{
    struct ksiginfo info = {
        .code = SI_USER,
        .pid = current ? current->tgid : 0,
    };
    return do_raise_info(p, sig, &info);
}

int do_raise_info(struct task *p, int sig, const struct ksiginfo *info)
{
    if (sig <= 0 || sig >= _NSIG) {
        klog(LOG_ERROR, "Invalid signal %d\n", sig);
        return -EINVAL;
    }
#ifdef DEBUG_SIGNAL
    klog(LOG_DEBUG, "Raising signal %d for process %d\n", sig, p->pid);
#endif
    sigset_t pending = p->pending;
    struct ksigaction *ka = &p->sighand->actions[sig];

    if (sig == SIGCONT && p->state == TASK_STOPPED) {
        klog(LOG_DEBUG, "Continuing stopped process %d due to SIGCONT\n", p->pid);
        set_task_running(p);
    }

    if (sigismember(&pending, sig)) {
        klog(LOG_DEBUG, "Signal %d already pending for process %d\n", sig, p->pid);
        return 0; // Signal already pending
    }

    if (ka->sa.sa_handler == SIG_IGN && !sigismember(&synchronous, sig)) {
        klog(LOG_DEBUG, "Signal %d ignored by process %d\n", sig, p->pid);
        return 0; // Ignored signal
    }

    // Default action is to ignore: discard now so it can't interrupt
    // pause/sigsuspend or blocking syscalls (still queued if blocked)
    if (ka->sa.sa_handler == SIG_DFL && (sigbit(sig) & DFL_IGN_SIG) &&
            !sigisblocked(p, sig)) {
        return 0;
    }

    p->siginfo[sig] = *info;
    sigaddset(&p->pending, sig);

    if (sigisblocked(p, sig)) {
        klog(LOG_DEBUG, "Signal %d is currently blocked for process %d\n", sig, p->pid);
        if (sigismember(&synchronous, sig) && p == current) {
            do_kill(p, sig);
        }
    }

    // Pairs with the barrier in prepare_wait (via its locks): either the waiter
    // sees the pending signal, or we see it sleeping and wake it
    atomic_thread_fence(memory_order_seq_cst);
    if (READ_ONCE(p->state) == TASK_SLEEPING && !sigisblocked(p, sig)) {
        klog(LOG_DEBUG, "Waking up process %d for signal %d\n", p->pid, sig);
        p->flags.interrupted = 1;
        set_task_running(p);
    }

    return 0;
}

int kill_pgrp(int pgid, int sig)
{
    klog(LOG_DEBUG, "kill_pgrp called with pgid=%d, sig=%d\n", pgid, sig);
    if (sig < 0 || sig >= _NSIG)
        return -EINVAL;

    if (pgid < 0)
        return -ESRCH;

    struct task *p;
    // TODO: Expand
    pid_t targets[64];
    int n = 0;
    unsigned long flags;
    acquire_lock_irqsave(&tasklist_lock, &flags);
    pgrp_for_each(p, pgid) {
        if (n < (int)(sizeof(targets) / sizeof(targets[0])))
            targets[n++] = p->pid;
    }
    release_lock_irqrestore(&tasklist_lock, flags);

    bool found = n > 0;
    for (int i = 0; i < n; i++) {
        p = get_task_by_pid(targets[i]);
        if (!p)
            continue;
        klog(LOG_DEBUG, "Sending signal %d to process %d in group %d\n", sig, p->pid, pgid);
        if (sig)
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
    if (sig < 0 || sig >= _NSIG)
        return -EINVAL;

    if (pid == 0) {
        return kill_pgrp(current->pgid, sig);
    } else if (pid == -1) {
        return -EOPNOTSUPP;
    } else if (pid < -1) {
        return kill_pgrp(-pid, sig);
    }

    struct task *p = get_task_by_pid(pid);
    if (!p || p->state == TASK_ZOMBIE) {
        klog(LOG_DEBUG, "kill: No such process %d\n", pid);
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

    struct ksigaction *ka = &current->sighand->actions[sig];
    __sighandler_t old_handler = ka->sa.sa_handler;
    ka->sa.sa_handler = handler;
    ka->sa.sa_flags = 0;
    ka->sa.sa_mask = 0;

    klog(LOG_DEBUG, "Signal %d handler set to %p\n", sig, handler);
    return (long)old_handler;
}

SYSCALL_DECL3(sigaction, int, signum, const struct sigaction *, act, struct sigaction *, oldact)
{
    if (signum <= 0 || signum >= _NSIG || signum == SIGKILL || signum == SIGSTOP)
        return -EINVAL;

    struct ksigaction *ka = &current->sighand->actions[signum];

    if (oldact && copy_to_user(oldact, ka, sizeof(struct sigaction))) {
        klog(LOG_WARN, "sigaction: Failed to copy old action to user memory\n");
        return -EFAULT;
    }

    if (act && copy_from_user(ka, act, sizeof(struct sigaction))) {
        klog(LOG_WARN, "sigaction: Failed to copy new action from user memory\n");
        return -EFAULT;
    }
    ka->sa.sa_mask = SIG_APPLY_MASK(ka->sa.sa_mask, unblockable);
#ifdef DEBUG_SIGNAL
    klog(LOG_DEBUG, "sigaction: Signal %d action is set to %p\n", signum, ka->sa.sa_handler);
#endif
    return 0;
}

SYSCALL_DECL3(sigprocmask, int, how, const sigset_t *, set, sigset_t *, oldset)
{
    if (set && !access_ok(set, sizeof(sigset_t))) {
        klog(LOG_WARN, "sigprocmask: Invalid user memory for new mask\n");
        return -EFAULT;
    }

    if (oldset && put_user(current->blocked, oldset)) {
        klog(LOG_WARN, "sigprocmask: Failed to copy old mask to user memory\n");
        return -EFAULT;
    }

    if (set) {
        sigset_t sigset;
        if (get_user(sigset, set)) return -EFAULT;
        sigset_t new_mask = SIG_APPLY_MASK(sigset, unblockable);
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

    // *set = current->pending;
    sigset_t pending = current->pending;
    if (put_user(pending, set)) return -EFAULT;
    klog(LOG_DEBUG, "sigpending: Pending signals for process %d: 0x%lx\n", current->pid, pending);
    return 0;
}

SYSCALL_DECL1(sigsuspend, sigset_t*, set)
{
    if (!access_ok(set, sizeof(sigset_t))) {
        klog(LOG_WARN, "sigsuspend: Invalid user memory for pending signals\n");
        return -EFAULT;
    }

    sigset_t mask;
    if (get_user(mask, set)) return -EFAULT;

    sigset_t newmask = SIG_APPLY_MASK(mask, unblockable);

    sigset_t oldmask = current->blocked;
    klog(LOG_DEBUG, "sigsuspend: changing mask from %lx to %lx", oldmask, newmask);
    current->blocked = newmask;

    while (!sigispending(current)) {
        current->state = TASK_SLEEPING;
        yield();
    }

    // Keep the temporary mask until the signal is delivered; the handler's
    // frame (or the exit path, if nothing is delivered) restores oldmask
    current->saved_sigmask = oldmask;
    current->flags.restore_sigmask = 1;
    return -EINTR;
}

static int do_tkill(int tgid, int tid, int sig)
{
    if (sig < 0 || sig >= _NSIG)
        return -EINVAL;
    struct task *p = get_task_by_pid(tid);
    if (!p || p->state == TASK_ZOMBIE || (tgid > 0 && p->tgid != tgid))
        return -ESRCH;
    if (sig == 0)
        return 0;
    return do_raise(p, sig);
}

SYSCALL_DECL3(tgkill, int, tgid, int, tid, int, sig)
{
    if (tgid <= 0 || tid <= 0)
        return -EINVAL;
    return do_tkill(tgid, tid, sig);
}

SYSCALL_DECL2(tkill, int, tid, int, sig)
{
    if (tid <= 0)
        return -EINVAL;
    return do_tkill(0, tid, sig);
}

// SIGKILL every other thread in p's group; they exit with status code
void zap_other_threads(struct task *p, int code)
{
    pid_t tids[64];
    int n = 0, bkt;
    struct task *t;
    unsigned long flags;

    acquire_lock_irqsave(&tasklist_lock, &flags);
    hash_for_each(pid_table, bkt, t, pid_hash) {
        if (t != p && t->tgid == p->tgid && t->state != TASK_ZOMBIE &&
                n < (int)(sizeof(tids) / sizeof(tids[0])))
            tids[n++] = t->pid;
    }
    release_lock_irqrestore(&tasklist_lock, flags);

    for (int i = 0; i < n; i++) {
        t = get_task_by_pid(tids[i]);
        if (!t)
            continue;
        t->group_exit_code = code;
        t->group_exit = true;
        do_raise(t, SIGKILL);
    }
}
