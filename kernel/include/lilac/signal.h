#ifndef _LILAC_SIGNAL_H
#define _LILAC_SIGNAL_H

#include <lib/list.h>
#include <lilac/signo.h>
#include <user/signal-defs.h>
// #include <user/siginfo.h>

#define sigbit(sig) (1UL << ((sig) - 1))

#define _SIGHUP		sigbit(SIGHUP)
#define _SIGINT		sigbit(SIGINT)
#define _SIGQUIT	sigbit(SIGQUIT)
#define _SIGILL		sigbit(SIGILL)
#define _SIGTRAP	sigbit(SIGTRAP)
#define _SIGABRT	sigbit(SIGABRT)
#define _SIGIOT		sigbit(SIGIOT)
#define _SIGBUS		sigbit(SIGBUS)
#define _SIGFPE		sigbit(SIGFPE)
#define _SIGKILL	sigbit(SIGKILL)
#define _SIGUSR1	sigbit(SIGUSR1)
#define _SIGSEGV	sigbit(SIGSEGV)
#define _SIGUSR2	sigbit(SIGUSR2)
#define _SIGPIPE	sigbit(SIGPIPE)
#define _SIGALRM	sigbit(SIGALRM)
#define _SIGTERM	sigbit(SIGTERM)
#define _SIGSTKFLT	sigbit(SIGSTKFLT)
#define _SIGCHLD	sigbit(SIGCHLD)
#define _SIGCONT	sigbit(SIGCONT)
#define _SIGSTOP	sigbit(SIGSTOP)
#define _SIGTSTP	sigbit(SIGTSTP)
#define _SIGTTIN	sigbit(SIGTTIN)
#define _SIGTTOU	sigbit(SIGTTOU)
#define _SIGURG		sigbit(SIGURG)
#define _SIGXCPU	sigbit(SIGXCPU)
#define _SIGXFSZ	sigbit(SIGXFSZ)
#define _SIGVTALRM	sigbit(SIGVTALRM)
#define _SIGPROF	sigbit(SIGPROF)
#define _SIGWINCH	sigbit(SIGWINCH)
#define _SIGIO		sigbit(SIGIO)
#define _SIGPOLL	sigbit(SIGPOLL)
#define _SIGPWR		sigbit(SIGPWR)
#define _SIGSYS		sigbit(SIGSYS)
#define _SIGUNUSED	sigbit(SIGUNUSED)

#define _NSIG 32
#define _NSIG_WORDS (_NSIG / (sizeof(unsigned long) * 8))

typedef unsigned long sigset_t;


typedef struct kernel_siginfo {
    //__SIGINFO;
} kernel_siginfo_t;

struct sigpending {
    struct list_head list;
    sigset_t signal;
};

struct sigaction {
    union {
        __sighandler_t	sa_handler;
        // void		(*sa_sigaction)(int, kernel_siginfo_t*, void *);
    };
    unsigned long	sa_flags;
    __sigrestore_t  sa_restorer;
    sigset_t	    sa_mask;
};

struct ksigaction {
    struct sigaction sa;
};

// #define sa_handler sa.sa_handler
// #define sa_flags sa.sa_flags

struct ksignal {
    struct ksigaction ka;
    // kernel_siginfo_t info;
    int sig;
};

typedef struct ucontext {
    struct ucontext *uc_link;
    sigset_t uc_sigmask;
    stack_t uc_stack;
    // struct mcontext uc_mcontext; // architecture-specific
} ucontext_t;

struct task;

#define SIG_APPLY_MASK(set, mask) ((set) & ~(mask))

#define sig_get_active(t) SIG_APPLY_MASK((t)->pending, (t)->blocked)

#define sigisblocked(t, sig) (((t)->blocked & sigbit(sig)) != 0)
#define sigispending(t) (sig_get_active(t) != 0)
#define sigaddset(set, sig) (*(set) |= sigbit(sig))
#define sigdelset(set, sig) (*(set) &= ~sigbit(sig))
#define sigemptyset(set) (*(set) = 0)
#define sigfillset(set) (*(set) = ~0UL)
#define sigismember(set, sig) ((*(set) & sigbit(sig)) != 0)

// queue a signal for delivery to a task
int do_raise(struct task *p, int sig);
// immediate termination of the process
void do_kill(struct task *p, int sig);
int handle_signal(void);
int kill_pgrp(int pgid, int sig);

#endif
