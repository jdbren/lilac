#include <lilac/signal.h>

#include <lilac/lilac.h>
#include <lilac/syscall.h>
#include <lilac/process.h>
#include <lilac/wait.h>

#define TERM_SIG (_SIGHUP | _SIGINT | _SIGKILL | _SIGPIPE | _SIGALRM | _SIGTERM)
#define CORE_SIG (_SIGQUIT | _SIGILL | _SIGABRT | _SIGSEGV | _SIGFPE)
#define STOP_SIG (_SIGSTOP | _SIGTSTP | _SIGTTIN | _SIGTTOU)

int handle_signal(void)
{
    int sig = __builtin_ffs(current->pending.signal) - 1;
    if (sig == -1) {
        klog(LOG_DEBUG, "No pending signals to handle\n");
        return 0;
    }

    struct ksigaction *ka = &current->sighandlers->actions[sig];
    int sig_bit = (1 << sig);
    current->pending.signal &= ~sig_bit;
    if (current->pending.signal == 0)
        current->flags.sig_pending = 0;

    if (ka->sa_handler == SIG_IGN) {
        klog(LOG_DEBUG, "Signal %d ignored by process %d\n", sig, current->pid);
        return 0;
    }

    if (ka->sa_handler == SIG_DFL) {
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
    }
    return 0;
}

int do_raise(struct task *p, int sig)
{
    if (sig <= 0 || sig >= _NSIG) {
        klog(LOG_ERROR, "Invalid signal %d\n", sig);
        return -EINVAL;
    }
    p->pending.signal |= (1 << sig);
    p->flags.sig_pending = 1;
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
    ka->sa_handler = handler;

    klog(LOG_DEBUG, "Signal %d handler set to %p\n", sig, handler);
    return 0;
}
