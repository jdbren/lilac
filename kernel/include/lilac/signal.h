#ifndef _LILAC_SIGNAL_H
#define _LILAC_SIGNAL_H

#include <lib/list.h>
#include <user/signal-defs.h>
#include <user/signo.h>
// #include <user/siginfo.h>

#define _SIGHUP		(1 << SIGHUP)
#define _SIGINT		(1 << SIGINT)
#define _SIGQUIT	(1 << SIGQUIT)
#define _SIGILL		(1 << SIGILL)
#define _SIGTRAP	(1 << SIGTRAP)
#define _SIGABRT	(1 << SIGABRT)
#define _SIGIOT		(1 << SIGIOT)
#define _SIGBUS		(1 << SIGBUS)
#define _SIGFPE		(1 << SIGFPE)
#define _SIGKILL	(1 << SIGKILL)
#define _SIGUSR1	(1 << SIGUSR1)
#define _SIGSEGV	(1 << SIGSEGV)
#define _SIGUSR2	(1 << SIGUSR2)
#define _SIGPIPE	(1 << SIGPIPE)
#define _SIGALRM	(1 << SIGALRM)
#define _SIGTERM	(1 << SIGTERM)
#define _SIGSTKFLT	(1 << SIGSTKFLT)
#define _SIGCHLD	(1 << SIGCHLD)
#define _SIGCONT	(1 << SIGCONT)
#define _SIGSTOP	(1 << SIGSTOP)
#define _SIGTSTP	(1 << SIGTSTP)
#define _SIGTTIN	(1 << SIGTTIN)
#define _SIGTTOU	(1 << SIGTTOU)
#define _SIGURG		(1 << SIGURG)
#define _SIGXCPU	(1 << SIGXCPU)
#define _SIGXFSZ	(1 << SIGXFSZ)
#define _SIGVTALRM	(1 << SIGVTALRM)
#define _SIGPROF	(1 << SIGPROF)
#define _SIGWINCH	(1 << SIGWINCH)
#define _SIGIO		(1 << SIGIO)
#define _SIGPOLL	(1 << SIGPOLL)
#define _SIGPWR		(1 << SIGPWR)
#define _SIGSYS		(1 << SIGSYS)
#define _SIGUNUSED	(1 << SIGUNUSED)

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
    sigset_t	    sa_mask;
};

struct ksigaction {
    struct sigaction sa;
    __sigrestore_t sa_restorer;
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

#define sigisblocked(t, sig) ((t)->blocked & (1 << (sig)))
#define sigispending(t) ((t)->flags.sig_pending)

int do_raise(struct task *p, int sig);
int handle_signal(void);
int kill_pgrp(int pgid, int sig);

#endif
